#ifndef SCANNER_H
#define SCANNER_H

#include <Arduino.h>

//=============================================================================
// Pin Definitions
//=============================================================================

// Built-in LED (GPIO2 on most ESP32 dev boards)
static const uint8_t LED_PIN = 2;

// Debug RGB LEDs (using output-capable GPIOs)
// Note: GPIO 34-39 are input-only on ESP32!
static const uint8_t LED_RED   = 25;   // D25
static const uint8_t LED_GREEN = 26;   // D26
static const uint8_t LED_BLUE  = 27;   // D27

//=============================================================================
// Timing Constants
//=============================================================================

static const unsigned long ROM_RETRY_INTERVAL = 5000;  // Retry ROM ID every 5 seconds
static const unsigned long CYCLE_DELAY = 10;           // Min delay between reads (ms)
static const unsigned long BLINK_SLOW = 1000;          // Slow blink interval (waiting)
static const unsigned long BLINK_FAST = 200;           // Fast blink interval (connected)

//=============================================================================
// DTC Code Names (from b10scan.asm)
//=============================================================================

static const char* dtcNames[] = {
  // Byte 1 (bits 0-6)
  "11-Crank", "12-StartSw", "13-Cam", "14-Inj1", "15-Inj2", "16-Inj3", "17-Inj4", nullptr,
  // Byte 2 (bits 0-7)  
  "21-Temp", "22-Knock", "23-MAF", "24-IAC", "31-TPS", "32-O2", "33-VSS", "35-Purge",
  // Byte 3 (bits 0-7)
  "41-FuelTrim", "42-IdleSw", nullptr, "44-WGC", "45-Baro", "49-WrongMAF", "51-NeutSw", "52-ParkSw"
};

//=============================================================================
// Scanner Function Declarations
// (ECU communication functions are in ecu_comms.h)
//=============================================================================

// Scanner Operations
bool verifyRomId();
void printCurrentReading(int idx);
void readTroubleCodes();
void clearTroubleCodes();
void processSerialCommand();

// LED helpers
void setStatusLED(bool red, bool green, bool blue);

#endif // SCANNER_H
