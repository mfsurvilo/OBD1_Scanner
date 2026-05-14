#ifndef LED_H
#define LED_H

#include <Arduino.h>

//=============================================================================
// LED Status Module
// Handles RGB LED status indication and patterns
//=============================================================================

namespace LED {

//-----------------------------------------------------------------------------
// LED Color Flags (can be OR'd together)
//-----------------------------------------------------------------------------
constexpr uint8_t RED    = 0x01;
constexpr uint8_t GREEN  = 0x02;
constexpr uint8_t BLUE   = 0x04;
constexpr uint8_t YELLOW = 0x08;  // RED + GREEN combined, or separate yellow LED
constexpr uint8_t ALL    = 0x0F;

//-----------------------------------------------------------------------------
// Blink Patterns
//-----------------------------------------------------------------------------
enum Pattern {
  SOLID,
  FAST_BLINK,
  SLOW_BLINK,
  FADE,
  BREATHE
};

//-----------------------------------------------------------------------------
// System States (high-level, overrides individual settings)
//-----------------------------------------------------------------------------
enum State {
  STATE_NONE,       // No override, use manual LED control
  STATE_WAITING,    // Waiting for ECU - slow green blink
  STATE_CONNECTED,  // ECU connected - fast blue blink
  STATE_ERROR,      // Error condition - fast red blink
  STATE_IDLE        // Idle/standby - dim green
};

//-----------------------------------------------------------------------------
// Public Functions
//-----------------------------------------------------------------------------

// Initialize LED pins
void setup();

// Update LED patterns (call from loop)
void update();

// Turn specific LEDs on (use OR: set_LED_on(RED | BLUE))
void set_LED_on(uint8_t colors);

// Turn specific LEDs off
void set_LED_off(uint8_t colors);

// Turn all LEDs on
void set_all_LED_on();

// Turn all LEDs off
void set_all_LED_off();

// Set blink pattern for specific LEDs
void set_LED_blink(Pattern pattern, uint8_t colors);

// Set system state (overrides individual LED settings)
void set_LED_state(State state);

// Get current state
State get_LED_state();

// Legacy compatibility
bool isConnected();

} // namespace LED

#endif // LED_H
