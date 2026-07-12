#include "web_server.h"
#include "version.h"
#include "version_compare.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

WebServerClass WebServerApp;

//=============================================================================
// Setup
//=============================================================================
void WebServerClass::begin() {
  // --- WiFi: host our AP AND (if provisioned) join home Wi-Fi for internet --
  WiFi.mode(WIFI_AP_STA);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CLIENTS);
  _ip = WiFi.softAPIP().toString();
  Serial.printf("[net] AP %s : %s  (%s)\n", AP_SSID, _ip.c_str(),
                apOk ? "up" : "FAILED");
  startSta();   // non-blocking; connects in the background if creds are saved

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

  // Internet (STA) features: Wi-Fi provisioning + pull-OTA from GitHub releases.
  _http.on("/wifi", HTTP_POST, [this]() { handleWifiPost(); });
  _http.on("/update/check", HTTP_GET, [this]() { handleUpdateCheck(); });
  _http.on("/update/pull", HTTP_POST, [this]() { handleUpdatePull(); });

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
  doc["version"]   = FW_VERSION;
  doc["commit"]    = FW_GIT_COMMIT;
  doc["build"]     = FW_BUILD_DATE;
  doc["uptime_ms"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["clients"]   = clientCount();

  // Home-Wi-Fi (STA) state — drives the "internet update" UI in the PWA.
  bool staUp = (WiFi.status() == WL_CONNECTED);
  doc["sta_connected"] = staUp;
  doc["sta_ssid"]      = _staSsid;
  doc["sta_ip"]        = staUp ? WiFi.localIP().toString() : String("");

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
// Home-Wi-Fi (STA) provisioning
//=============================================================================
void WebServerClass::startSta() {
  Preferences prefs;
  prefs.begin("wifi", true);              // read-only
  _staSsid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();
  if (_staSsid.isEmpty()) {
    Serial.println("[net] STA: no saved credentials");
    return;
  }
  Serial.printf("[net] STA: connecting to \"%s\" ...\n", _staSsid.c_str());
  WiFi.begin(_staSsid.c_str(), pass.c_str());  // non-blocking; polled via /status
}

void WebServerClass::handleWifiPost() {
  String ssid = _http.arg("ssid");
  String pass = _http.arg("pass");
  if (ssid.isEmpty()) {
    _http.send(400, "application/json", "{\"ok\":false,\"msg\":\"ssid required\"}");
    return;
  }
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  _staSsid = ssid;
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("[net] STA: saved creds, connecting to \"%s\"\n", ssid.c_str());
  _http.send(200, "application/json", "{\"ok\":true,\"msg\":\"saved, connecting\"}");
}

//=============================================================================
// Internet update: version check + pull-OTA from the highest-versioned release
//
// We deliberately do NOT use GitHub's /releases/latest — it picks "latest" by
// the release's created_at timestamp, which is unreliable when releases are
// created out of order (e.g. two tags pushed together). Instead we list all
// releases and pick the highest version number ourselves.
//
// TLS uses setInsecure() (no certificate validation) for simplicity. For a
// hardened build, pin GitHub's CA instead.
//=============================================================================

bool WebServerClass::fetchLatestTag(String& tag) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  String url = String("https://api.github.com/repos/") + GH_REPO + "/releases?per_page=30";
  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", "obd1-scanner");   // GitHub API requires this
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  // Filter the (large) array response down to the fields we need per element.
  JsonDocument filter;
  JsonObject f = filter.add<JsonObject>();
  f["tag_name"]   = true;
  f["draft"]      = true;
  f["prerelease"] = true;
  JsonDocument doc;
  DeserializationError e =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (e) return false;

  long best = -1;
  for (JsonObject r : doc.as<JsonArray>()) {
    if ((r["draft"] | false) || (r["prerelease"] | false)) continue;
    String t = r["tag_name"].as<String>();
    if (t.isEmpty()) continue;
    long k = version_key(t.c_str());
    if (k > best) { best = k; tag = t; }
  }
  return best >= 0;
}

void WebServerClass::handleUpdateCheck() {
  JsonDocument doc;
  doc["current"] = FW_VERSION;
  if (WiFi.status() != WL_CONNECTED) {
    doc["ok"] = false;
    doc["msg"] = "device is not connected to the internet";
  } else {
    String tag;
    if (fetchLatestTag(tag)) {
      doc["ok"] = true;
      doc["latest"] = tag;
      doc["update_available"] = (tag != String(FW_VERSION));
    } else {
      doc["ok"] = false;
      doc["msg"] = "could not reach GitHub";
    }
  }
  String out;
  serializeJson(doc, out);
  _http.send(200, "application/json", out);
}

// Stream one raw image (firmware.bin or filesystem.bin) from a URL straight
// into the Update library. `command` is U_FLASH (app) or U_SPIFFS (LittleFS).
// On U_FLASH success the boot partition is switched to the new slot (pending
// verify); the caller reverts it if a later step fails.
bool WebServerClass::pullFile(const String& url, int command, String& err) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // github.com -> objects.*
  if (!http.begin(client, url)) { err = "begin failed"; return false; }
  http.addHeader("User-Agent", "obd1-scanner");
  int code = http.GET();
  if (code != HTTP_CODE_OK) { err = "HTTP " + String(code); http.end(); return false; }

  int total = http.getSize();          // -1 if chunked/unknown
  // The filesystem partition can't be written while it's mounted.
  if (command == U_SPIFFS && _fsMounted) { LittleFS.end(); _fsMounted = false; }
  // U_FLASH: bound by the free (other) app slot. U_SPIFFS: size from the header
  // if known, else let Update size it from the spiffs partition.
  size_t maxSize = (command == U_FLASH)
                     ? ESP.getFreeSketchSpace()
                     : (total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN);
  if (!Update.begin(maxSize, command)) {
    Update.printError(Serial); err = "Update.begin failed"; http.end(); return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  int received = 0;
  unsigned long lastData = millis();
  uint8_t buf[1024];

  while ((http.connected() || stream->available() > 0) &&
         (total < 0 || received < total)) {
    size_t avail = stream->available();
    if (avail == 0) {
      if (millis() - lastData > 15000) { err = "stalled"; break; }
      delay(2);
      continue;
    }
    lastData = millis();
    int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
    if (n <= 0) continue;
    if (Update.write(buf, n) != (size_t)n) {
      Update.printError(Serial); err = "flash write error"; break;
    }
    received += n;
  }
  http.end();

  if (!err.isEmpty()) { Update.abort(); return false; }
  if (total > 0 && received != total) { Update.abort(); err = "download truncated"; return false; }
  if (!Update.end(true)) { Update.printError(Serial); err = "Update.end failed"; return false; }
  Serial.printf("[ota] pulled %d bytes (%s), flashed OK\n",
                received, command == U_FLASH ? "fw" : "fs");
  return true;
}

void WebServerClass::handleUpdatePull() {
  if (WiFi.status() != WL_CONNECTED) {
    _http.send(200, "application/json",
               "{\"ok\":false,\"msg\":\"device is not connected to the internet\"}");
    return;
  }
  // Resolve the highest-versioned release, then pull its assets by explicit tag
  // (not /releases/latest, whose ordering is unreliable — see fetchLatestTag).
  String tag;
  if (!fetchLatestTag(tag)) {
    _http.send(500, "application/json",
               "{\"ok\":false,\"msg\":\"could not find the latest release\"}");
    return;
  }
  const String base = String("https://github.com/") + GH_REPO +
                      "/releases/download/" + tag + "/";
  String err;

  Serial.printf("[ota] pulling %sfirmware.bin\n", base.c_str());
  if (!pullFile(base + "firmware.bin", U_FLASH, err)) {
    if (!_fsMounted) _fsMounted = LittleFS.begin(true);
    _http.sendHeader("Connection", "close");
    _http.send(500, "application/json",
               String("{\"ok\":false,\"msg\":\"firmware: ") + err + "\"}");
    return;
  }

  // App slot written + boot switched. Now the filesystem; if it fails, revert
  // the boot partition so the device never comes up half-updated.
  Serial.printf("[ota] pulling %sfilesystem.bin\n", base.c_str());
  if (!pullFile(base + "filesystem.bin", U_SPIFFS, err)) {
    const esp_partition_t* run = esp_ota_get_running_partition();
    if (run && esp_ota_set_boot_partition(run) == ESP_OK)
      Serial.println("[ota] reverted boot partition after filesystem failure");
    if (!_fsMounted) _fsMounted = LittleFS.begin(true);
    _http.sendHeader("Connection", "close");
    _http.send(500, "application/json",
               String("{\"ok\":false,\"msg\":\"filesystem: ") + err + "\"}");
    return;
  }

  _http.sendHeader("Connection", "close");
  _http.send(200, "application/json", "{\"ok\":true,\"msg\":\"updated, rebooting\"}");
  _rebootPending = true;
  _rebootAt = millis() + 500;
}
