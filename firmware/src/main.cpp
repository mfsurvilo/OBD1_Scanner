#include <Arduino.h>
#include "LED.h"
#include "wifi_server.h"
#include "logger.h"
#include "ECU.h"
#include "pin_defs.h"
#include "scaling_config.h"


void setup() {
  Serial.begin(DEBUG_BAUD);
  Log.begin();
  logInfoPersist("System startup");
  
  LED.setup();
  ScalingConfig.begin();
  DataServer.setup();
  ECU.setup();
}

void loop() {
  Log.update();  // Flush setup log to serial when connected
  LED.update();
  DataServer.update();
  ECU.update();
}