#include <Arduino.h>
#include "LED.h"
#include "wifi_server.h"
#include "logger.h"
#include "ECU.h"
#include "pin_defs.h"
#include "scaling_config.h"


// The ECU talks over a slow K-line and its connect handshake blocks for up to
// ~2 s when the car isn't answering. Running it in its own FreeRTOS task pinned
// to core 0 keeps that blocking off the main loop (core 1), so the status LED
// and web server stay responsive. Shared state it touches (DataServer JSON,
// poll factors, the logger) is mutex-guarded in those modules.
static void ecuTask(void* pv) {
  for (;;) {
    ECU.update();
    vTaskDelay(pdMS_TO_TICKS(5));  // yield; ECU.update() has its own pacing gate
  }
}

void setup() {
  Serial.begin(DEBUG_BAUD);
  Log.begin();
  logInfoPersist("System startup");

  LED.setup();
  ScalingConfig.begin();
  DataServer.setup();
  ECU.setup();

  // Start the ECU task after all singletons (and their mutexes) are initialized.
  xTaskCreatePinnedToCore(ecuTask, "ecu", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
  Log.update();  // Flush setup log to serial when connected
  LED.update();
  DataServer.update();
}