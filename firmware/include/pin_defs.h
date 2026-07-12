#ifndef PIN_DEFS_H
#define PIN_DEFS_H

#include <Arduino.h>

//=============================================================================
// Pin Definitions  (ESP32-S3-WROOM-1 on the OBD1_Scanner board)
//=============================================================================

// Status RGB LED (LED1). Pins from the KiCad schematic nets LED1_R/G/B.
// NOTE: do NOT use GPIO 26-32 on the ESP32-S3 — they are wired to the internal
// SPI flash/PSRAM and driving them causes a flash watchdog reset.
static const uint8_t LED_GREEN = 4;   // IO4  (LED1_R)
static const uint8_t LED_BLUE  = 5;   // IO5  (LED1_G)
static const uint8_t LED_RED   = 6;   // IO6  (LED1_B)

// The RGB LED is COMMON-ANODE: the common pin sits at 3.3V, so a color lights
// when its GPIO is driven LOW (sinks current). HIGH = off.
static const uint8_t RGB_ON  = LOW;
static const uint8_t RGB_OFF = HIGH;

// TX/RX activity LEDs (nets LED_TX / LED_RX). Cathode to GND -> ACTIVE-HIGH.
static const uint8_t LED_TX_PIN = 12; // IO12 (LED_TX)
static const uint8_t LED_RX_PIN = 13; // IO13 (LED_RX)

//=============================================================================
// Serial
//=============================================================================
static const uint32_t DEBUG_BAUD = 115200;  // USB debug serial

#endif // PIN_DEFS_H
