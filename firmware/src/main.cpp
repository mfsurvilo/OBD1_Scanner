#include <Arduino.h>
#include "ecu_defs.h"
#include "ecu_comms.h"
#include "scanner.h"
#include "serial_debug.h"
#include "LED.h"

// Subaru SSM1 OBD1 Scanner - ESP32-WROOM
// Connects to ECU and cycles through parameters
// Debug output via USB serial (non-blocking)

// Calculate number of parameters from array size
const int NUM_PARAMS = sizeof(ecuParams) / sizeof(ecuParams[0]);

// Track last poll time for each parameter
unsigned long lastPollTime[32];  // Support up to 32 parameters

byte rx[3];
int currentIndex = 0;
bool romVerified = false;
unsigned long lastRomAttempt = 0;

void setup() {
  // Initialize LEDs (shows boot sequence)
  LED::setup();
  
  // Initialize poll times
  for (int i = 0; i < 32; i++) {
    lastPollTime[i] = 0;
  }
  
  // Initialize debug serial (non-blocking)
  debug_init();
  
  // Initialize ECU communication (real or simulated)
  ecu_init();
  
  debug_println("=================================");
  if (isSimulationMode()) {
    debug_println("Subaru SSM1 Scanner - SIMULATION");
  } else {
    debug_println("Subaru SSM1 OBD1 Scanner - ESP32");
  }
  debug_println("=================================");
  debug_println();
  debug_printf("Expected ECU: %s (ROM: %s)\n", ECU_NAME, ECU_ROM_ID);
  debug_println();
  debug_println("Commands: DTC (read codes), CLEAR (clear codes)");
  debug_println();
  if (isSimulationMode()) {
    debug_println("*** SIMULATION MODE - No ECU required ***");
    debug_println("Simulating ECU connection delay...");
    
    // Simulate waiting-for-ECU period (4 seconds)
    unsigned long simStart = millis();
    while (millis() - simStart < 4000) {
      LED::update();
      delay(10);
    }
  } else {
    debug_println("Attempting to connect to ECU...");
  }
}

void loop() {
  // Check for serial commands
  processSerialCommand();
  
  // Update LED blink state
  LED::update();
  
  // If not connected, try to get ROM ID
  if (!LED::isConnected()) {
    if (millis() - lastRomAttempt >= ROM_RETRY_INTERVAL || lastRomAttempt == 0) {
      lastRomAttempt = millis();
      
      romVerified = verifyRomId();
      
      if (romVerified) {
        LED::set_LED_state(LED::STATE_CONNECTED);
        debug_println();
        debug_printf("Starting continuous read of %d parameters...\n", NUM_PARAMS);
        debug_separator();
        
        // Read trouble codes once on connection
        readTroubleCodes();
        debug_separator();
        
        currentIndex = 0;
      } else {
        debug_printf("Retrying in %lu seconds...\n", ROM_RETRY_INTERVAL / 1000);
      }
    }
    return;
  }
  
  // Connected - poll parameters based on their individual rates
  unsigned long now = millis();
  
  // Find next parameter that needs polling
  for (int i = 0; i < NUM_PARAMS; i++) {
    int idx = (currentIndex + i) % NUM_PARAMS;
    
    if (!ecuParams[idx].enabled) continue;
    
    if (now - lastPollTime[idx] >= ecuParams[idx].pollRate) {
      // Time to poll this parameter
      printCurrentReading(idx);
      lastPollTime[idx] = now;
      currentIndex = (idx + 1) % NUM_PARAMS;
      delay(CYCLE_DELAY);  // Small delay between reads
      break;
    }
  }
}

void processSerialCommand() {
  String cmd = debug_readCommand();
  if (cmd.length() == 0) return;
  
  if (cmd == "DTC") {
    debug_separator();
    readTroubleCodes();
    debug_separator();
  } else if (cmd == "CLEAR") {
    debug_separator();
    clearTroubleCodes();
    debug_separator();
  }
}

bool verifyRomId() {
  if (isSimulationMode()) {
    debug_printf("SIMULATION: ROM ID = %s\n", ECU_ROM_ID);
    debug_println();
    debug_println("*** SIMULATION MODE ACTIVE ***");
    debug_printf("ECU: %s\n", ECU_NAME);
    return true;
  }
  
  debug_println("Pulling ROM ID...");
  
  if (!ECU_GetROMID(rx)) {
    debug_println("ERROR: Failed to read ROM ID from ECU");
    debug_println("Check connections.");
    return false;
  }
  
  char receivedId[7];
  sprintf(receivedId, "%02X%02X%02X", rx[0], rx[1], rx[2]);
  
  debug_printf("Received ROM ID: %s\n", receivedId);
  debug_printf("Expected ROM ID: %s\n", ECU_ROM_ID);
  
  String received = String(receivedId);
  String expected = String(ECU_ROM_ID);
  received.toUpperCase();
  expected.toUpperCase();
  
  if (received.startsWith(expected) || expected.startsWith(received)) {
    debug_println();
    debug_println("*** ROM ID MATCH - ECU Connected! ***");
    debug_printf("ECU: %s\n", ECU_NAME);
    return true;
  } else {
    debug_println();
    debug_println("*** WARNING: ROM ID MISMATCH ***");
    debug_println("Proceeding anyway - data may be incorrect!");
    return true;
  }
}

void printCurrentReading(int idx) {
  unsigned int addr = ecuParams[idx].addr;
  const char* name = ecuParams[idx].name;
  
  int readout = ecu_read(addr);
  
  if (readout >= 0) {
    debug_printf("[%d] 0x%X (%s): Raw=%d (0x%X)\n", idx, addr, name, readout, readout);
  } else if (readout == -1) {
    debug_printf("[%d] 0x%X (%s): ERROR: No response\n", idx, addr, name);
  } else if (readout == -2) {
    debug_printf("[%d] 0x%X (%s): ERROR: Invalid response\n", idx, addr, name);
  } else {
    debug_printf("[%d] 0x%X (%s): ERROR: %d\n", idx, addr, name, readout);
  }
}

void readTroubleCodes() {
  debug_println("[DTC] Reading Trouble Codes...");
  
  // Check if DTC addresses are valid (not all zeros)
  if (troubleCodes.activeAddr[0] == 0 && troubleCodes.activeAddr[1] == 0) {
    debug_println("[DTC] ERROR: DTC addresses not defined for this ECU");
    return;
  }
  
  bool anyActive = false;
  bool anyStored = false;
  
  // Read active codes
  debug_println("[DTC] Active Codes:");
  for (int byteNum = 0; byteNum < 3; byteNum++) {
    if (troubleCodes.activeAddr[byteNum] == 0) continue;
    
    int val = ecu_read(troubleCodes.activeAddr[byteNum]);
    if (val < 0) {
      debug_printf("[DTC] ERROR reading address 0x%X\n", troubleCodes.activeAddr[byteNum]);
      continue;
    }
    
    // Check each bit
    for (int bit = 0; bit < 8; bit++) {
      if (val & (1 << bit)) {
        int nameIdx = byteNum * 8 + bit;
        if (nameIdx < 24 && dtcNames[nameIdx] != NULL) {
          debug_printf("[DTC] ACTIVE: %s\n", dtcNames[nameIdx]);
          anyActive = true;
        }
      }
    }
  }
  
  if (!anyActive) {
    debug_println("[DTC] No active codes");
  }
  
  // Read stored codes
  debug_println("[DTC] Stored Codes:");
  for (int byteNum = 0; byteNum < 3; byteNum++) {
    if (troubleCodes.storedAddr[byteNum] == 0) continue;
    
    int val = ecu_read(troubleCodes.storedAddr[byteNum]);
    if (val < 0) {
      debug_printf("[DTC] ERROR reading address 0x%X\n", troubleCodes.storedAddr[byteNum]);
      continue;
    }
    
    for (int bit = 0; bit < 8; bit++) {
      if (val & (1 << bit)) {
        int nameIdx = byteNum * 8 + bit;
        if (nameIdx < 24 && dtcNames[nameIdx] != NULL) {
          debug_printf("[DTC] STORED: %s\n", dtcNames[nameIdx]);
          anyStored = true;
        }
      }
    }
  }
  
  if (!anyStored) {
    debug_println("[DTC] No stored codes");
  }
}

void clearTroubleCodes() {
  debug_println("[DTC] Clearing Trouble Codes...");
  
  if (troubleCodes.clearAddr == 0) {
    debug_println("[DTC] ERROR: Clear address not defined for this ECU");
    return;
  }
  
  // From b10scan.asm: send clear command 4 times with delays
  // Command is 0xAA + address (2 bytes) + value
  for (int i = 0; i < 4; i++) {
    if (ecu_write(troubleCodes.clearAddr, troubleCodes.clearValue)) {
      debug_printf("[DTC] Clear attempt %d/4 sent\n", i + 1);
    } else {
      debug_printf("[DTC] Clear attempt %d/4 FAILED\n", i + 1);
    }
    delay(180);  // 180ms between attempts (from b10scan.asm)
  }
  
  debug_println("[DTC] Clear complete. Turn off ignition to finalize.");
  
  // Re-read to verify
  delay(500);
  readTroubleCodes();
}