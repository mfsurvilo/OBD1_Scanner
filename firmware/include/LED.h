#ifndef LED_H
#define LED_H

#include <Arduino.h>

//=============================================================================
// LED Status Class
// Handles RGB LED status indication and patterns
// Queries ECU and DataServer for system state
//=============================================================================

// LED Color Flags (can be OR'd together)
constexpr uint8_t LED_RED_FLAG    = 0x01;
constexpr uint8_t LED_GREEN_FLAG  = 0x02;
constexpr uint8_t LED_BLUE_FLAG   = 0x04;
constexpr uint8_t LED_YELLOW_FLAG = 0x08;
constexpr uint8_t LED_ALL_FLAGS   = 0x0F;

// Blink Patterns
enum LEDPattern {
  LED_SOLID,
  LED_FAST_BLINK,
  LED_SLOW_BLINK,
  LED_FADE,
  LED_BREATHE
};

class LEDController {
public:
  LEDController();
  
  void setup(unsigned long updateInterval = 20);
  bool update();
  void selfCheck();
  
  void setOn(uint8_t colors);
  void setOff(uint8_t colors);
  void setAllOn();
  void setAllOff();
  void setBlink(LEDPattern pattern, uint8_t colors);

private:
  void _applyColors(uint8_t colors);
  void _applyColorsPWM(uint8_t colors, uint8_t brightness);
  unsigned long _getPatternInterval(LEDPattern p);
  void _runPattern(LEDPattern pattern, uint8_t colors);
  
  // Individual LED update functions
  void _updateRed();
  void _updateGreen();
  void _updateBlue();
  
  // PWM cleanup - call after using applyColorsPWM to restore digitalWrite
  void _restoreGPIOMode();
  
  LEDPattern _pattern;
  uint8_t _blinkColors;
  uint8_t _currentColors;
  unsigned long _lastUpdate;
  uint8_t _fadeValue;
  bool _fadeDirection;
  bool _blinkState;
  unsigned long _updateInterval;
  unsigned long _lastModuleUpdate;
  
  // Per-LED timing and state
  unsigned long _redLastUpdate;
  unsigned long _greenLastUpdate;
  unsigned long _blueLastUpdate;
  bool _redState;
  bool _greenState;
  bool _blueState;
};

// Global instance
extern LEDController LED;

#endif // LED_H
