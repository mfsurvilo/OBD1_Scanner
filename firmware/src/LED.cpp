#include "LED.h"
#include "scanner.h"

//=============================================================================
// LED Status Module Implementation
//=============================================================================

namespace LED {

//-----------------------------------------------------------------------------
// Internal State
//-----------------------------------------------------------------------------
static State _currentState = STATE_NONE;
static Pattern _pattern = SOLID;
static uint8_t _blinkColors = 0;
static uint8_t _currentColors = 0;
static unsigned long _lastUpdate = 0;
static uint8_t _fadeValue = 0;
static bool _fadeDirection = true;  // true = increasing
static bool _blinkState = false;

//-----------------------------------------------------------------------------
// Pin Mapping
//-----------------------------------------------------------------------------
static void applyColors(uint8_t colors) {
  // YELLOW = RED + GREEN
  bool red = (colors & RED) || (colors & YELLOW);
  bool green = (colors & GREEN) || (colors & YELLOW);
  bool blue = (colors & BLUE);
  
  digitalWrite(LED_RED, red ? HIGH : LOW);
  digitalWrite(LED_GREEN, green ? HIGH : LOW);
  digitalWrite(LED_BLUE, blue ? HIGH : LOW);
  digitalWrite(LED_PIN, (red || green || blue) ? HIGH : LOW);
}

static unsigned long getPatternInterval(Pattern p) {
  switch (p) {
    case FAST_BLINK: return 200;
    case SLOW_BLINK: return 1000;
    case FADE:       return 20;
    case BREATHE:    return 30;
    default:         return 1000;
  }
}

//-----------------------------------------------------------------------------
// Public Functions
//-----------------------------------------------------------------------------

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  set_all_LED_off();
  
  // Boot flash sequence
  for (int i = 0; i < 4; i++) {
    set_LED_on(GREEN);
    delay(100);
    set_all_LED_off();
    delay(100);
  }
  
  // Start in waiting state
  set_LED_state(STATE_WAITING);
}

void update() {
  unsigned long now = millis();
  uint8_t colors = _blinkColors;
  Pattern pattern = _pattern;
  
  // State overrides
  if (_currentState != STATE_NONE) {
    switch (_currentState) {
      case STATE_WAITING:
        colors = GREEN;
        pattern = SLOW_BLINK;
        break;
      case STATE_CONNECTED:
        colors = BLUE;
        pattern = FAST_BLINK;
        break;
      case STATE_ERROR:
        colors = RED;
        pattern = FAST_BLINK;
        break;
      case STATE_IDLE:
        colors = GREEN;
        pattern = SOLID;
        break;
      default:
        break;
    }
  }
  
  unsigned long interval = getPatternInterval(pattern);
  
  if (now - _lastUpdate >= interval) {
    _lastUpdate = now;
    
    switch (pattern) {
      case SOLID:
        applyColors(colors);
        break;
        
      case FAST_BLINK:
      case SLOW_BLINK:
        _blinkState = !_blinkState;
        applyColors(_blinkState ? colors : 0);
        break;
        
      case FADE:
      case BREATHE:
        if (_fadeDirection) {
          _fadeValue += 10;
          if (_fadeValue >= 250) _fadeDirection = false;
        } else {
          _fadeValue -= 10;
          if (_fadeValue <= 5) {
            _fadeDirection = true;
            if (pattern == BREATHE) {
              // Breathe has a pause at bottom
              delay(200);
            }
          }
        }
        // Simple on/off threshold for digital LEDs
        applyColors(_fadeValue > 127 ? colors : 0);
        break;
    }
  }
}

void set_LED_on(uint8_t colors) {
  _currentState = STATE_NONE;  // Manual control overrides state
  _currentColors |= colors;
  applyColors(_currentColors);
}

void set_LED_off(uint8_t colors) {
  _currentState = STATE_NONE;
  _currentColors &= ~colors;
  applyColors(_currentColors);
}

void set_all_LED_on() {
  _currentState = STATE_NONE;
  _currentColors = ALL;
  applyColors(ALL);
}

void set_all_LED_off() {
  _currentState = STATE_NONE;
  _currentColors = 0;
  applyColors(0);
}

void set_LED_blink(Pattern pattern, uint8_t colors) {
  _currentState = STATE_NONE;
  _pattern = pattern;
  _blinkColors = colors;
  _fadeValue = 0;
  _fadeDirection = true;
}

void set_LED_state(State state) {
  _currentState = state;
  _fadeValue = 0;
  _fadeDirection = true;
  _blinkState = false;
}

State get_LED_state() {
  return _currentState;
}

bool isConnected() {
  return _currentState == STATE_CONNECTED;
}

} // namespace LED
