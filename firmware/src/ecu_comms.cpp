#include <Arduino.h>
#include "ecu_comms.h"
#include "ecu_defs.h"

//=============================================================================
// ECU Communication Layer
// Uses real serial when built normally, simulation when -DSIMULATE_ECU
//=============================================================================

#ifdef SIMULATE_ECU
#include "simulation_data.h"

// Simulation state
static int simFrameIndex = 0;
static unsigned long lastSimAdvance = 0;
static const int NUM_PARAMS = sizeof(ecuParams) / sizeof(ecuParams[0]);

// Realistic ECU timing at 1953 baud:
// - 1953 baud = ~195 bytes/sec = ~5.1ms per byte
// - 4-byte command + 50ms delay + 3-byte response = ~85ms total
static const int SIM_ECU_RESPONSE_MS = 85;

#else
// ESP32 Serial2 pins for ECU communication
#define ECU_RX_PIN 16
#define ECU_TX_PIN 17
#endif

void ecu_init() {
#ifdef SIMULATE_ECU
  randomSeed(analogRead(0));
#else
  Serial2.begin(1953, SERIAL_8E1, ECU_RX_PIN, ECU_TX_PIN);
  Serial2.setTimeout(300);
#endif
}

bool isSimulationMode() {
#ifdef SIMULATE_ECU
  return true;
#else
  return false;
#endif
}

void ECU_Stop() {
#ifndef SIMULATE_ECU
  byte txbuf[4] = {0x12, 0x00, 0x00, 0x00};
  Serial2.write(txbuf, 4);
  delay(50);
  Serial2.flush();
#endif
}

int ecu_read(unsigned int addr) {
#ifdef SIMULATE_ECU
  // Simulate realistic ECU response time
  delay(SIM_ECU_RESPONSE_MS);
  
  // Advance simulation frame periodically (simulates changing engine conditions)
  if (millis() - lastSimAdvance >= SIM_FRAME_INTERVAL) {
    lastSimAdvance = millis();
    simFrameIndex = (simFrameIndex + 1) % SIM_FRAMES;
  }
  
  // Find which parameter index this address corresponds to
  for (int i = 0; i < NUM_PARAMS; i++) {
    if (ecuParams[i].addr == addr) {
      int baseVal = simData[simFrameIndex][i];
      // Add slight noise like real sensor data, but clamp to valid range
      int noise = random(-2, 3);
      int result = baseVal + noise;
      return (result < 0) ? 0 : (result > 255) ? 255 : result;
    }
  }
  
  // Check if it's a DTC address
  for (int i = 0; i < 3; i++) {
    if (addr == troubleCodes.activeAddr[i]) return simActiveDTC[i];
    if (addr == troubleCodes.storedAddr[i]) return simStoredDTC[i];
  }
  
  return 128;  // Default value for unknown addresses
#else
  byte txbuf[4] = {0x78, (byte)(addr >> 8), (byte)(addr & 0xFF), 0x00};
  
  ECU_Stop();
  while (Serial2.read() >= 0);
  
  Serial2.write(txbuf, 4);
  delay(50);
  Serial2.flush();
  
  char response[3] = {0};
  int num = Serial2.readBytes(response, 3);
  
  if (num != 3) return -1;
  if (response[0] == (char)((addr >> 8) & 0xFF) && 
      response[1] == (char)(addr & 0xFF)) {
    return (byte)response[2];
  }
  return -2;
#endif
}

bool ecu_write(unsigned int addr, byte value) {
#ifdef SIMULATE_ECU
  // Simulate realistic ECU write time
  delay(SIM_ECU_RESPONSE_MS);
  return true;
#else
  byte txbuf[4] = {0xAA, (byte)(addr >> 8), (byte)(addr & 0xFF), value};
  
  ECU_Stop();
  while (Serial2.read() >= 0);
  Serial2.write(txbuf, 4);
  delay(50);
  Serial2.flush();
  
  return true;
#endif
}

boolean ECU_GetROMID(byte* buffer) {
#ifdef SIMULATE_ECU
  // Simulate ECU wake-up and ROM ID read (multiple retries like real ECU)
  delay(SIM_ECU_RESPONSE_MS * 3);  // Takes longer for initial connection
  
  const char* romId = ECU_ROM_ID;
  auto hexChar = [](char c) -> byte {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
  };
  
  int len = strlen(romId);
  if (len >= 6) {
    buffer[0] = (hexChar(romId[0]) << 4) | hexChar(romId[1]);
    buffer[1] = (hexChar(romId[2]) << 4) | hexChar(romId[3]);
    buffer[2] = (hexChar(romId[4]) << 4) | hexChar(romId[5]);
  } else if (len >= 4) {
    buffer[0] = (hexChar(romId[0]) << 4) | hexChar(romId[1]);
    buffer[1] = (hexChar(romId[2]) << 4) | hexChar(romId[3]);
    buffer[2] = 0x00;
  } else {
    buffer[0] = buffer[1] = buffer[2] = 0x00;
    return false;
  }
  return true;
#else
  char romidCmd[4] = {0x00, 0x46, 0x48, 0x49};
  char romid[3] = {0};
  
  ECU_Stop();
  while (Serial2.read() >= 0);
  ecu_read(0x1337);  // Wake up
  
  for (int retries = 0; retries < 8; retries++) {
    Serial2.write(romidCmd, 4);
    int nbytes = Serial2.readBytes(romid, 3);
    if ((nbytes == 3) && (romid[0] != 0x00)) break;
  }
  
  buffer[0] = romid[0];
  buffer[1] = romid[1];
  buffer[2] = romid[2];
  
  return (romid[0] != 0x00);
#endif
}
