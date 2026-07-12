//=============================================================================
// OBD1 Scanner — framework skeleton
//
// Bare-minimum RTOS structure to build on:
//   - blinkTask : status-LED heartbeat (proof the scheduler is alive), core 1.
//   - netTask   : AP + HTTP/WebSocket server + OTA update, core 0.
//
// Upload firmware & PWA:
//   USB : ./upload.sh              (firmware via esptool + LittleFS via uploadfs)
//   WiFi: join AP "OBD1_Scanner", open http://obd1.local, use the OTA form,
//         or POST a .bin to /update/firmware or /update/filesystem.
//=============================================================================
#include <Arduino.h>
#include <esp_ota_ops.h>
#include "version.h"
#include "pin_defs.h"
#include "web_server.h"

// Status-LED heartbeat color.
#define BLINK_LED LED_GREEN

// Common-anode RGB helper: `on` lights the color (drives the pin LOW).
static inline void ledWrite(uint8_t pin, bool on) {
  digitalWrite(pin, on ? RGB_ON : RGB_OFF);
}

// Heartbeat: a short blink ~1 Hz on the build-selected color. Runs on core 1,
// fully independent of the network stack, so a visible pulse means the RTOS
// scheduler is healthy.
static void blinkTask(void* pv) {
  const uint8_t leds[] = {LED_RED, LED_GREEN, LED_BLUE};
  for (uint8_t p : leds) { pinMode(p, OUTPUT); ledWrite(p, false); }

  for (;;) {
    ledWrite(BLINK_LED, true);
    vTaskDelay(pdMS_TO_TICKS(80));
    ledWrite(BLINK_LED, false);
    vTaskDelay(pdMS_TO_TICKS(920));
  }
}

// Network: owns the (potentially blocking) WiFi/HTTP/OTA work on core 0, off
// the core running the heartbeat.
static void netTask(void* pv) {
  WebServerApp.begin();
  for (;;) {
    WebServerApp.loop();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// OTA rollback (bootloader rollback is enabled in this core). A freshly-OTA'd
// image boots in PENDING_VERIFY and must confirm itself healthy. If it never
// confirms and the device later resets, the bootloader automatically reverts to
// the previous slot — we do NOT force that reboot ourselves in normal builds,
// because on a fresh flash the other slot may be empty and a self-reboot would
// loop forever. A plain USB/factory flash isn't on probation, so this no-ops.
static void confirmHealthIfPending() {
  static bool handled = false;
  if (handled) return;

  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (!running || esp_ota_get_state_partition(running, &st) != ESP_OK ||
      st != ESP_OTA_IMG_PENDING_VERIFY) {
    handled = true;  // not on probation — nothing to confirm
    return;
  }

#ifdef FORCE_UNHEALTHY
  // Fault-injection build (env:badhealth) ONLY: never confirm, and after
  // a grace period actively roll back to prove auto-revert works. The HIL test
  // guarantees a valid previous slot, so this can't loop. Never ship this.
  if (millis() > 8000) {
    Serial.println("[ota] (test) forcing rollback to previous image");
    esp_ota_mark_app_invalid_rollback_and_reboot();  // does not return
  }
#else
  // Normal build: confirm once the network stack is up; otherwise just keep
  // running unconfirmed (safe — no self-reboot, so it can never loop).
  if (WebServerApp.ok()) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[ota] new image confirmed healthy — rollback cancelled");
    handled = true;
  }
#endif
}

void setup() {
  Serial.begin(DEBUG_BAUD);
  delay(300);
  Serial.printf("\n%s %s (%s)\nbuilt %s\n",
                FW_NAME, FW_VERSION, FW_GIT_COMMIT, FW_BUILD_DATE);

  xTaskCreatePinnedToCore(blinkTask, "blink", 2048, nullptr, 1, nullptr, 1);
  // 16 KB stack: the internet pull-OTA does a TLS handshake (mbedTLS) which
  // needs more headroom than the default 8 KB.
  xTaskCreatePinnedToCore(netTask,   "net",  16384, nullptr, 1, nullptr, 0);
}

void loop() {
  // All work happens in tasks; the loop just runs the rollback health check.
  confirmHealthIfPending();
  vTaskDelay(pdMS_TO_TICKS(1000));
}
