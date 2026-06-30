#ifndef PIN_DEFS_H
#define PIN_DEFS_H

#include <Arduino.h>

//=============================================================================
// Pin Definitions
//=============================================================================

// Built-in LED (GPIO2 on most ESP32 dev boards)
static const uint8_t LED_BUILTIN_PIN = 2;

// Debug RGB LEDs (using output-capable GPIOs)
// Note: GPIO 34-39 are input-only on ESP32!
static const uint8_t LED_RED   = 27;   // D27
static const uint8_t LED_GREEN = 26;   // D26
static const uint8_t LED_BLUE  = 25;   // D25

//=============================================================================
// Serial Pins
//=============================================================================

// Debug Serial (USB) - ESP32 default UART0
// Note: These are typically hardwired to USB-Serial chip on dev boards
static const uint8_t DEBUG_TX_PIN = 1;   // USB Serial TX
static const uint8_t DEBUG_RX_PIN = 3;   // USB Serial RX
static const uint32_t DEBUG_BAUD = 115200;

// ECU Serial (Serial2) - SSM1 protocol at 1953 baud
static const uint8_t ECU_TX_PIN = 17;    // To ECU K-line driver
static const uint8_t ECU_RX_PIN = 16;    // From ECU K-line driver
static const uint32_t ECU_BAUD = 1953;

#endif // PIN_DEFS_H
