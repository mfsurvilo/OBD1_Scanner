#include "LED.h"
#include "pin_defs.h"
#include "ECU.h"
#include "wifi_server.h"

//=============================================================================
// LED Controller Implementation
//
// Only one color is ever lit at a time. The mode is chosen from system state
// every update, with errors taking priority:
//   MODE_ERROR      (ECU or WiFi error)  -> blinking red
//   MODE_HEARTBEAT  (WiFi client)        -> blue 1 Hz blink
//   MODE_PULSE      (waiting for client) -> pulsing (breathing) blue
//=============================================================================

// Global instance
LEDController LED;

// Common-anode LED: analogWrite duty is inverted (255 = off, low = bright).
static inline uint8_t pwmDuty(uint8_t brightness) { return 255 - brightness; }

//=============================================================================
// Public Methods
//=============================================================================

LEDController::LEDController()
  : _updateInterval(20)
  , _lastModuleUpdate(0)
  , _mode(MODE_PULSE)
  , _lastUpdate(0)
  , _fadeValue(0)
  , _fadeDirection(true)
  , _pwmActive(false)
  , _phase(0)
  , _blinkState(false)
{
}

void LEDController::setup(unsigned long updateInterval) {
  _updateInterval = updateInterval;
  _lastModuleUpdate = 0;

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  _allOff();

  selfCheck();

  // Start in the pulse (waiting-for-client) mode.
  _enterMode(MODE_PULSE, millis());
}

bool LEDController::update() {
  unsigned long now = millis();

  if (now - _lastModuleUpdate < _updateInterval) {
    return false;
  }
  _lastModuleUpdate = now;

  // Pick the mode from system state (errors win).
  ECUState ecuState = ECU.getState();
  WiFiState wifiState = DataServer.getState();

  LedMode mode;
  if (ecuState == ECU_ERROR || wifiState == WIFI_ERROR) {
    mode = MODE_ERROR;
  } else if (wifiState == WIFI_CONNECTED) {
    mode = MODE_HEARTBEAT;
  } else {
    mode = MODE_PULSE;
  }

  if (mode != _mode) {
    _enterMode(mode, now);
  }

  switch (_mode) {
    case MODE_PULSE:     _runPulse(now);     break;
    case MODE_HEARTBEAT: _runHeartbeat(now); break;
    case MODE_ERROR:     _runError(now);     break;
  }

  return true;
}

void LEDController::selfCheck() {
  const uint8_t pins[] = {LED_RED, LED_GREEN, LED_BLUE};

  for (int c = 0; c < 3; c++) {
    int fade = 0;
    bool up = true;
    unsigned long start = millis();

    while (millis() - start < 2000) {
      if (up) {
        fade += 10;
        if (fade >= 250) up = false;
      } else {
        fade -= 10;
        if (fade <= 5) up = true;
      }
      analogWrite(pins[c], pwmDuty(fade));
      delay(20);
    }

    analogWrite(pins[c], 255);  // off before moving to next color
  }

  _restoreGPIOMode();
}

//=============================================================================
// Private Methods
//=============================================================================

void LEDController::_enterMode(LedMode mode, unsigned long now) {
  // Leaving a PWM (pulse) mode: return the blue pin to plain digital control.
  if (_pwmActive && mode != MODE_PULSE) {
    _restoreGPIOMode();
  }

  _mode = mode;
  _lastUpdate = now;
  _allOff();

  switch (mode) {
    case MODE_PULSE:
      _fadeValue = 0;
      _fadeDirection = true;
      _pwmActive = true;
      break;

    case MODE_HEARTBEAT:
      _blinkState = true;
      digitalWrite(LED_BLUE, RGB_ON);  // start lit
      break;

    case MODE_ERROR:
      _blinkState = true;
      digitalWrite(LED_RED, RGB_ON);
      break;
  }
}

void LEDController::_runPulse(unsigned long now) {
  // update() already gates on _updateInterval, so step the breathe each call.
  (void)now;
  if (_fadeDirection) {
    _fadeValue += 5;
    if (_fadeValue >= 250) _fadeDirection = false;
  } else {
    if (_fadeValue <= 5) {
      _fadeDirection = true;
    } else {
      _fadeValue -= 5;
    }
  }
  analogWrite(LED_BLUE, pwmDuty(_fadeValue));
}

void LEDController::_runHeartbeat(unsigned long now) {
  // 1 Hz blink while a client is connected: 500 ms on, 500 ms off.
  if (now - _lastUpdate >= 500) {
    _lastUpdate = now;
    _blinkState = !_blinkState;
    digitalWrite(LED_BLUE, _blinkState ? RGB_ON : RGB_OFF);
  }
}

void LEDController::_runError(unsigned long now) {
  if (now - _lastUpdate >= 200) {
    _lastUpdate = now;
    _blinkState = !_blinkState;
    digitalWrite(LED_RED, _blinkState ? RGB_ON : RGB_OFF);
  }
}

void LEDController::_allOff() {
  digitalWrite(LED_RED, RGB_OFF);
  digitalWrite(LED_GREEN, RGB_OFF);
  digitalWrite(LED_BLUE, RGB_OFF);
}

void LEDController::_restoreGPIOMode() {
  ledcDetachPin(LED_RED);
  ledcDetachPin(LED_GREEN);
  ledcDetachPin(LED_BLUE);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  _allOff();
  _pwmActive = false;
}
