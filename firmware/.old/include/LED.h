#ifndef LED_H
#define LED_H

#include <Arduino.h>

//=============================================================================
// LED Status Class
// Drives a single-color-at-a-time status RGB LED based on system state:
//   - Waiting for a WiFi client   -> pulsing (breathing) blue
//   - WiFi client connected       -> blue 1 Hz blink
//   - ECU or WiFi error           -> blinking red
// State is polled from the ECU and DataServer singletons.
//=============================================================================

class LEDController {
public:
  LEDController();

  void setup(unsigned long updateInterval = 20);
  bool update();
  void selfCheck();

private:
  // The mutually-exclusive things the status LED can be showing.
  enum LedMode {
    MODE_PULSE,      // pulsing blue: waiting for a WiFi client
    MODE_HEARTBEAT,  // blue 1 Hz blink: WiFi client connected
    MODE_ERROR       // blinking red: ECU or WiFi error
  };

  void _enterMode(LedMode mode, unsigned long now);
  void _runPulse(unsigned long now);
  void _runHeartbeat(unsigned long now);
  void _runError(unsigned long now);

  void _allOff();
  void _restoreGPIOMode();  // detach PWM (ledc) and return pins to digital

  // Module update gating
  unsigned long _updateInterval;
  unsigned long _lastModuleUpdate;

  // Active mode + timing
  LedMode _mode;
  unsigned long _lastUpdate;

  // Pulse (PWM breathe) state
  uint8_t _fadeValue;
  bool _fadeDirection;
  bool _pwmActive;

  // Heartbeat / error blink state
  uint8_t _phase;
  bool _blinkState;
};

// Global instance
extern LEDController LED;

#endif // LED_H
