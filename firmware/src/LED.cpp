#include "LED.h"
#include "pin_defs.h"
#include "ECU.h"
#include "wifi_server.h"

//=============================================================================
// LED Controller Implementation
// Queries ECU and DataServer for system state to determine LED behavior
//=============================================================================

// Global instance
LEDController LED;

//=============================================================================
// Public Methods
//=============================================================================

LEDController::LEDController()
  : _pattern(LED_SOLID)
  , _blinkColors(0)
  , _currentColors(0)
  , _lastUpdate(0)
  , _fadeValue(0)
  , _fadeDirection(true)
  , _blinkState(false)
  , _updateInterval(20)
  , _lastModuleUpdate(0)
  , _redLastUpdate(0)
  , _greenLastUpdate(0)
  , _blueLastUpdate(0)
  , _redState(true)
  , _greenState(true)
  , _blueState(true)
{
}

void LEDController::setup(unsigned long updateInterval) {
  _updateInterval = updateInterval;
  _lastModuleUpdate = 0;
  
  pinMode(LED_BUILTIN_PIN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  setAllOff();
  
  selfCheck();
}

bool LEDController::update() {
  unsigned long now = millis();
  
  if (now - _lastModuleUpdate < _updateInterval) {
    return false;
  }
  _lastModuleUpdate = now;
  
  _updateRed();
  _updateGreen();
  _updateBlue();
  
  return true;
}

void LEDController::selfCheck() {
  uint8_t colors[] = {LED_RED_FLAG, LED_GREEN_FLAG, LED_BLUE_FLAG};
  
  for (int c = 0; c < 3; c++) {
    _fadeValue = 0;
    _fadeDirection = true;
    unsigned long start = millis();
    
    while (millis() - start < 2000) {
      if (_fadeDirection) {
        _fadeValue += 10;
        if (_fadeValue >= 250) _fadeDirection = false;
      } else {
        _fadeValue -= 10;
        if (_fadeValue <= 5) _fadeDirection = true;
      }
      _applyColorsPWM(colors[c], _fadeValue);
      delay(20);
    }
  }
  
  _restoreGPIOMode();
}

void LEDController::setOn(uint8_t colors) {
  _currentColors |= colors;
  _applyColors(_currentColors);
}

void LEDController::setOff(uint8_t colors) {
  _currentColors &= ~colors;
  _applyColors(_currentColors);
}

void LEDController::setAllOn() {
  _currentColors = LED_ALL_FLAGS;
  _applyColors(LED_ALL_FLAGS);
}

void LEDController::setAllOff() {
  _currentColors = 0;
  _applyColors(0);
}

void LEDController::setBlink(LEDPattern pattern, uint8_t colors) {
  _pattern = pattern;
  _blinkColors = colors;
  _fadeValue = 0;
  _fadeDirection = true;
}

//=============================================================================
// Private Methods
//=============================================================================

void LEDController::_restoreGPIOMode() {
  ledcDetachPin(LED_RED);
  ledcDetachPin(LED_GREEN);
  ledcDetachPin(LED_BLUE);
  ledcDetachPin(LED_BUILTIN_PIN);
  
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_BUILTIN_PIN, OUTPUT);
  
  setAllOff();
}

void LEDController::_applyColors(uint8_t colors) {
  bool red = (colors & LED_RED_FLAG) || (colors & LED_YELLOW_FLAG);
  bool green = (colors & LED_GREEN_FLAG) || (colors & LED_YELLOW_FLAG);
  bool blue = (colors & LED_BLUE_FLAG);
  
  digitalWrite(LED_RED, red ? HIGH : LOW);
  digitalWrite(LED_GREEN, green ? HIGH : LOW);
  digitalWrite(LED_BLUE, blue ? HIGH : LOW);
  digitalWrite(LED_BUILTIN_PIN, (red || green || blue) ? HIGH : LOW);
}

void LEDController::_applyColorsPWM(uint8_t colors, uint8_t brightness) {
  bool red = (colors & LED_RED_FLAG) || (colors & LED_YELLOW_FLAG);
  bool green = (colors & LED_GREEN_FLAG) || (colors & LED_YELLOW_FLAG);
  bool blue = (colors & LED_BLUE_FLAG);
  
  analogWrite(LED_RED, red ? brightness : 0);
  analogWrite(LED_GREEN, green ? brightness : 0);
  analogWrite(LED_BLUE, blue ? brightness : 0);
  analogWrite(LED_BUILTIN_PIN, (red || green || blue) ? brightness : 0);
}

unsigned long LEDController::_getPatternInterval(LEDPattern p) {
  switch (p) {
    case LED_FAST_BLINK: return 200;
    case LED_SLOW_BLINK: return 1000;
    case LED_FADE:       return 20;
    case LED_BREATHE:    return 20;
    default:             return 1000;
  }
}

void LEDController::_runPattern(LEDPattern pattern, uint8_t colors) {
  unsigned long now = millis();
  unsigned long interval = _getPatternInterval(pattern);
  
  if (now - _lastUpdate >= interval) {
    _lastUpdate = now;
    
    switch (pattern) {
      case LED_SOLID:
        _applyColors(colors);
        break;
        
      case LED_FAST_BLINK:
      case LED_SLOW_BLINK:
        _blinkState = !_blinkState;
        _applyColors(_blinkState ? colors : 0);
        break;
        
      case LED_FADE:
      case LED_BREATHE:
        if (_fadeDirection) {
          _fadeValue += 10;
          if (_fadeValue >= 250) _fadeDirection = false;
        } else {
          _fadeValue -= 10;
          if (_fadeValue <= 5) {
            _fadeDirection = true;
          }
        }
        _applyColorsPWM(colors, _fadeValue);
        break;
    }
  }
}

void LEDController::_updateRed() {
  ECUState ecuState = ECU.getState();
  WiFiState wifiState = DataServer.getState();
  
  bool hasError = (ecuState == ECU_ERROR || wifiState == WIFI_FAULT);
  
  if (hasError) {
    unsigned long now = millis();
    if (now - _redLastUpdate >= 200) {
      _redLastUpdate = now;
      _redState = !_redState;
    }
    digitalWrite(LED_RED, _redState ? HIGH : LOW);
  } else {
    digitalWrite(LED_RED, LOW);
    _redState = false;
  }
}

void LEDController::_updateGreen() {
  ECUState ecuState = ECU.getState();
  unsigned long now = millis();
  unsigned long interval;
  
  switch (ecuState) {
    case ECU_NOT_CONNECTED:
      interval = 500;
      break;
    case ECU_CONNECTED:
      interval = 100;
      break;
    case ECU_ERROR:
    default:
      digitalWrite(LED_GREEN, LOW);
      _greenState = false;
      return;
  }
  
  if (now - _greenLastUpdate >= interval) {
    _greenLastUpdate = now;
    _greenState = !_greenState;
  }
  digitalWrite(LED_GREEN, _greenState ? HIGH : LOW);
}

void LEDController::_updateBlue() {
  WiFiState wifiState = DataServer.getState();
  
  if (wifiState == WIFI_CONNECTED) {
    unsigned long now = millis();
    if (now - _blueLastUpdate >= 200) {
      _blueLastUpdate = now;
      _blueState = !_blueState;
    }
    digitalWrite(LED_BLUE, _blueState ? HIGH : LOW);
  } else {
    digitalWrite(LED_BLUE, LOW);
    _blueState = false;
  }
  
  bool anyOn = (digitalRead(LED_RED) == HIGH || 
                digitalRead(LED_GREEN) == HIGH || 
                digitalRead(LED_BLUE) == HIGH);
  digitalWrite(LED_BUILTIN_PIN, anyOn ? HIGH : LOW);
}
