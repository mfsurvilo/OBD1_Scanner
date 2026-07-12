#include "web_server.h"
#include "version.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>

WebServerClass WebServerApp;

//=============================================================================
// Setup
//=============================================================================
void WebServerClass::begin() {
  // --- WiFi access point (AP-only) -----------------------------------------
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CLIENTS);
  _ip = WiFi.softAPIP().toString();
  Serial.printf("[net] AP %s : %s  (%s)\n", AP_SSID, _ip.c_str(),
                apOk ? "up" : "FAILED");

  // --- Filesystem that holds the PWA ---------------------------------------
  _fsMounted = LittleFS.begin(true);
  Serial.printf("[net] LittleFS %s\n", _fsMounted ? "mounted" : "MOUNT FAILED");

  // --- mDNS so clients can use obd1.local ----------------------------------
  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    Serial.printf("[net] mDNS: %s.local\n", MDNS_HOST);
  }

  // --- Routes ---------------------------------------------------------------
  _http.on("/status", HTTP_GET, [this]() { handleStatus(); });

  // OTA: firmware and filesystem share the same chunk pump, differing only in
  // the Update command. The second lambda receives the upload chunks; the
  // first runs after the whole body arrives (success/failure reply + reboot).
  _http.on("/update/firmware", HTTP_POST,
           [this]() { finishUpdate(); },
           [this]() { handleUpload(U_FLASH); });
  _http.on("/update/filesystem", HTTP_POST,
           [this]() { finishUpdate(); },
           [this]() { handleUpload(U_SPIFFS); });
  _http.on("/update/combined", HTTP_POST,
           [this]() { finishCombined(); },
           [this]() { handleCombinedUpload(); });

  // Everything else -> static file from LittleFS (with SPA fallback).
  _http.onNotFound([this]() { streamOr404(); });

  _http.enableCORS(true);
  _http.begin();
  Serial.printf("[net] HTTP :%d\n", HTTP_PORT);

  _ws.begin();
  _ws.onEvent([this](uint8_t num, WStype_t type, uint8_t*, size_t) {
    if (type == WStype_CONNECTED) {
      String s = statusJson();
      _ws.sendTXT(num, s);
    }
  });
  Serial.printf("[net] WebSocket :%d\n", WS_PORT);

  // Health signal for OTA rollback: both the AP and the filesystem must be up.
  _ok = apOk && _fsMounted;
}

//=============================================================================
// Loop
//=============================================================================
void WebServerClass::loop() {
  _http.handleClient();
  _ws.loop();

  // Heartbeat broadcast ~1 Hz.
  unsigned long now = millis();
  if (now - _lastBroadcast >= 1000) {
    _lastBroadcast = now;
    if (_ws.connectedClients() > 0) {
      String s = statusJson();
      _ws.broadcastTXT(s);
    }
  }

  // Deferred reboot after a successful OTA (lets the HTTP reply flush first).
  if (_rebootPending && now >= _rebootAt) {
    Serial.println("[ota] rebooting...");
    delay(100);
    ESP.restart();
  }
}

//=============================================================================
// Status
//=============================================================================
int WebServerClass::clientCount() {
  return WiFi.softAPgetStationNum();
}

String WebServerClass::statusJson() {
  JsonDocument doc;
  doc["name"]      = FW_NAME;
  doc["variant"]   = FW_VARIANT;
  doc["version"]   = FW_VERSION;
  doc["commit"]    = FW_GIT_COMMIT;
  doc["build"]     = FW_BUILD_DATE;
  doc["uptime_ms"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["clients"]   = clientCount();

  // OTA / rollback introspection (used by the regression round-trip tests).
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (run) doc["partition"] = run->label;
  esp_ota_img_states_t st;
  if (run && esp_ota_get_state_partition(run, &st) == ESP_OK) {
    doc["ota_confirmed"] = (st != ESP_OTA_IMG_PENDING_VERIFY);
  } else {
    doc["ota_confirmed"] = true;   // USB-flashed / factory slot: not on probation
  }

  String out;
  serializeJson(doc, out);
  return out;
}

void WebServerClass::handleStatus() {
  _http.send(200, "application/json", statusJson());
}

//=============================================================================
// Static file serving (LittleFS) with single-page-app fallback
//=============================================================================
const char* WebServerClass::contentType(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".ico"))  return "image/x-icon";
  return "text/plain";
}

void WebServerClass::streamOr404() {
  String path = _http.uri();
  if (path.endsWith("/")) path += "index.html";

  if (!_fsMounted || !LittleFS.exists(path)) {
    // SPA fallback: serve index.html for unknown non-file routes.
    path = "/index.html";
  }

  if (!_fsMounted || !LittleFS.exists(path)) {
    _http.send(404, "text/plain",
               "No PWA on device. Run: pio run -t uploadfs");
    return;
  }

  File f = LittleFS.open(path, "r");
  _http.streamFile(f, contentType(path));
  f.close();
}

//=============================================================================
// OTA update — shared chunk pump for firmware (U_FLASH) and PWA (U_SPIFFS)
//=============================================================================
void WebServerClass::handleUpload(int command) {
  HTTPUpload& up = _http.upload();

  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] start: %s (%s)\n", up.filename.c_str(),
                  command == U_FLASH ? "firmware" : "filesystem");
    // The filesystem partition can't be written while it's mounted.
    if (command == U_SPIFFS && _fsMounted) {
      LittleFS.end();
      _fsMounted = false;
    }
    // U_FLASH: bound by the free (other) app slot. U_SPIFFS: let Update size
    // it from the spiffs partition.
    size_t maxSize = (command == U_FLASH) ? ESP.getFreeSketchSpace()
                                          : UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(maxSize, command)) {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[ota] done: %u bytes\n", up.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

void WebServerClass::finishUpdate() {
  bool ok = !Update.hasError();
  _http.sendHeader("Connection", "close");
  _http.send(ok ? 200 : 500, "application/json",
             ok ? "{\"ok\":true,\"msg\":\"update applied, rebooting\"}"
                : "{\"ok\":false,\"msg\":\"update failed\"}");
  if (ok) {
    _rebootPending = true;
    _rebootAt = millis() + 500;  // let the reply flush, then reboot in loop()
  } else if (!_fsMounted) {
    // A failed filesystem update left LittleFS unmounted; bring it back so the
    // PWA keeps serving instead of forcing a reboot on a bad upload.
    _fsMounted = LittleFS.begin(true);
  }
}

//=============================================================================
// Combined OTA — one .ota file carrying firmware + filesystem
//
// HTTP uploads arrive in arbitrary-sized chunks. All byte-accounting lives in
// the pure ota::Parser (include/ota_container.h, unit-tested natively); here we
// just drive the real Update library off the steps it emits.
//=============================================================================
void WebServerClass::handleCombinedUpload() {
  HTTPUpload& up = _http.upload();

  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] combined start: %s\n", up.filename.c_str());
    _ota = ota::Parser{};       // reset the streaming parser
    _otaIoError = false;
    _otaFwCommitted = false;
    return;
  }
  if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    _ota.phase = ota::P_ERROR;
    return;
  }
  if (up.status != UPLOAD_FILE_WRITE && up.status != UPLOAD_FILE_END) return;

  const uint8_t* data = up.buf;
  uint32_t len = up.currentSize;
  ota::Step step;

  while (!_otaIoError && ota::next(_ota, &data, &len, &step)) {
    switch (step.action) {
      case ota::BEGIN_FW:
        Serial.printf("[ota] combined: fw=%u fs=%u bytes\n",
                      _ota.header.fw_len, _ota.header.fs_len);
        if (!Update.begin(step.size, U_FLASH)) { Update.printError(Serial); _otaIoError = true; }
        break;
      case ota::WRITE_FW:
      case ota::WRITE_FS:
        if (Update.write((uint8_t*)step.data, step.len) != step.len) {
          Update.printError(Serial); _otaIoError = true;
        }
        break;
      case ota::END_FW:
        if (!Update.end(true)) { Update.printError(Serial); _otaIoError = true; }
        else { Serial.println("[ota] app slot written"); _otaFwCommitted = true; }
        break;
      case ota::BEGIN_FS:
        if (_fsMounted) { LittleFS.end(); _fsMounted = false; }
        if (!Update.begin(step.size, U_SPIFFS)) { Update.printError(Serial); _otaIoError = true; }
        break;
      case ota::END_FS:
        if (!Update.end(true)) { Update.printError(Serial); _otaIoError = true; }
        else Serial.println("[ota] filesystem written");
        break;
      case ota::NONE:
        break;
    }
  }

  if (up.status == UPLOAD_FILE_END && !_otaIoError && !ota::is_done(_ota)) {
    Serial.printf("[ota] combined image %s\n",
                  ota::is_error(_ota) ? "rejected (bad header)" : "truncated");
    Update.abort();
    _otaIoError = true;
  }
}

void WebServerClass::finishCombined() {
  bool ok = ota::is_done(_ota) && !_otaIoError && !Update.hasError();
  _http.sendHeader("Connection", "close");
  _http.send(ok ? 200 : 500, "application/json",
             ok ? "{\"ok\":true,\"msg\":\"firmware + app updated, rebooting\"}"
                : "{\"ok\":false,\"msg\":\"combined update failed\"}");
  if (ok) {
    _rebootPending = true;
    _rebootAt = millis() + 500;
  } else {
    // The app slot was already committed (boot switched) but the FS half
    // failed — revert the boot partition so we don't come up half-updated.
    if (_otaFwCommitted) {
      const esp_partition_t* run = esp_ota_get_running_partition();
      if (run && esp_ota_set_boot_partition(run) == ESP_OK) {
        Serial.println("[ota] reverted boot partition after partial failure");
      }
    }
    if (!_fsMounted) _fsMounted = LittleFS.begin(true);  // recover the FS
  }
  _otaFwCommitted = false;
  _ota = ota::Parser{};
}
