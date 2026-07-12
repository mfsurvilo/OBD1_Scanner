//=============================================================================
// Hardware Bring-Up Test
//
// Standalone sketch (no ECU / WiFi / app dependencies). Blinks every LED in
// sequence and prints status over USB serial so you can confirm the board is
// alive and each LED is wired correctly.
//
// Build/upload/monitor:
//   pio run -e hwtest -t upload -t monitor
//=============================================================================

#include <Arduino.h>
#include "pin_defs.h"

struct TestLED {
  const char* name;
  uint8_t     pin;
  bool        activeLow;  // true = drive LOW to light (common-anode RGB)
};

// RGB LED (D1) is common-anode -> active LOW.
// TX/RX indicators are single LEDs, cathode to GND -> active HIGH.
static const TestLED kLeds[] = {
  {"RED",   LED_RED,    true},
  {"GREEN", LED_GREEN,  true},
  {"BLUE",  LED_BLUE,   true},
  {"TX",    LED_TX_PIN, false},
  {"RX",    LED_RX_PIN, false},
};
static const size_t kNumLeds = sizeof(kLeds) / sizeof(kLeds[0]);

static const unsigned long kOnTimeMs  = 2000;   // how long each LED stays lit
static const unsigned long kGapMs     = 500;   // dark gap between LEDs
static unsigned long g_cycle = 0;

static void ledWrite(const TestLED& led, bool on) {
  // XOR with activeLow so the electrical level matches the desired state.
  digitalWrite(led.pin, (on != led.activeLow) ? HIGH : LOW);
}

static void allOff() {
  for (size_t i = 0; i < kNumLeds; i++) {
    ledWrite(kLeds[i], false);
  }
}

void setup() {
  Serial.begin(DEBUG_BAUD);
  delay(500);  // give USB CDC a moment to enumerate

  Serial.println();
  Serial.println("=====================================");
  Serial.println(" OBD1 Scanner - Hardware Test");
  Serial.println("=====================================");
  Serial.printf("LEDs under test: %u\n", (unsigned)kNumLeds);

  for (size_t i = 0; i < kNumLeds; i++) {
    pinMode(kLeds[i].pin, OUTPUT);
    Serial.printf("  %-8s -> GPIO%u\n", kLeds[i].name, kLeds[i].pin);
  }
  allOff();
  Serial.println("-------------------------------------");
}

void loop() {
  Serial.printf("[cycle %lu] uptime=%lu ms\n", g_cycle++, millis());

  for (size_t i = 0; i < kNumLeds; i++) {
    Serial.printf("  ON  %-8s (GPIO%u)\n", kLeds[i].name, kLeds[i].pin);
    ledWrite(kLeds[i], true);
    delay(kOnTimeMs);

    ledWrite(kLeds[i], false);
    Serial.printf("  OFF %-8s (GPIO%u)\n", kLeds[i].name, kLeds[i].pin);
    delay(kGapMs);
  }

  Serial.println("  --- sequence complete, pausing ---");
  delay(1000);
}
