#include "wifi_server.h"
#include "logger.h"
#include "scaling_config.h"
#include "ecu_defs.h"
#include "ECU.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

//=============================================================================
// DataServer Implementation
//=============================================================================

// Global instance
DataServerClass DataServer;

//=============================================================================
// Public Methods
//=============================================================================

DataServerClass::DataServerClass()
  : _state(WIFI_UNCONNECTED)
  , _fatalError(false)
  , _server(HTTP_PORT)
  , _wsServer(WEBSOCKET_PORT)
  , _lastJsonData("{}")
  , _ipAddress("")
  , _lastDataTime(0)
  , _updateInterval(100)
  , _lastModuleUpdate(0)
  , _dataChanged(false)
  , _dataMutex(nullptr)
{
}

void DataServerClass::setup(unsigned long updateInterval) {
  _updateInterval = updateInterval;
  _lastModuleUpdate = 0;

  if (!_dataMutex) _dataMutex = xSemaphoreCreateMutex();

  Serial.println("=== WiFi AP Setup ===");

  // AP-only: the scanner hosts its own "OBD1_Scanner" access point that the
  // phone/PWA connects to directly.
  WiFi.mode(WIFI_AP);
  bool success = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);

  if (success) {
    Serial.println("WiFi AP started successfully!");
  } else {
    Serial.println("ERROR: WiFi AP failed to start!");
    Log.setup("WiFi AP failed to start");
    _fatalError = true;  // no AP means no client can ever connect
  }

  IPAddress ip = WiFi.softAPIP();
  _ipAddress = ip.toString();

  Log.setupf("WiFi AP: %s / %s", AP_SSID, _ipAddress.c_str());

  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("AP IP Address: ");
  Serial.println(_ipAddress);

  // Mount the filesystem that holds the PWA (uploaded via `pio run -t uploadfs`).
  if (!LittleFS.begin(true)) {
    Serial.println("ERROR: LittleFS mount failed");
    Log.setup("LittleFS mount failed");
    _fatalError = true;  // no filesystem means the PWA cannot be served
  } else {
    Serial.println("LittleFS mounted");
  }

  // mDNS so clients can use obd1.local instead of chasing a DHCP address.
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    MDNS.addService("ws", "tcp", WEBSOCKET_PORT);
    Serial.printf("mDNS started: %s.local\n", MDNS_HOSTNAME);
  } else {
    Serial.println("ERROR: mDNS failed to start");
  }

  Serial.print("HTTP Port: ");
  Serial.println(HTTP_PORT);
  Serial.print("WebSocket Port: ");
  Serial.println(WEBSOCKET_PORT);
  Serial.println("======================");

  _server.on("/", HTTP_GET, [this]() { _handleRoot(); });
  _server.on("/data", HTTP_GET, [this]() { _handleData(); });
  _server.on("/data", HTTP_OPTIONS, [this]() { _handleCors(); });
  _server.on("/log", HTTP_GET, [this]() { _handleLog(); });
  _server.on("/log/ram", HTTP_GET, [this]() { _handleLogRAM(); });
  _server.on("/log/ram/clear", HTTP_GET, [this]() { _handleLogClear(); });
  _server.on("/log/flash", HTTP_GET, [this]() { _handleLogFlash(); });
  _server.on("/log/flash/clear", HTTP_GET, [this]() { _handleLogFlashClear(); });
  _server.on("/params", HTTP_GET, [this]() { _handleParams(); });
  _server.on("/poll", HTTP_POST, [this]() { _handlePollPost(); });
  _server.on("/poll", HTTP_OPTIONS, [this]() { _handleCors(); });
  _server.on("/scaling", HTTP_GET, [this]() { _handleScalingGet(); });
  _server.on("/scaling", HTTP_POST, [this]() { _handleScalingPost(); });
  _server.on("/scaling", HTTP_OPTIONS, [this]() { _handleCors(); });
  _server.onNotFound([this]() { _handleNotFound(); });
  
  _server.enableCORS(true);
  _server.begin();
  Serial.println("HTTP server started");

  // WebSocket server for live data streaming to the PWA.
  _wsServer.begin();
  _wsServer.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    _onWsEvent(num, type, payload, length);
  });
  Serial.println("WebSocket server started");

  _jsonDoc.clear();
  _dataArray = _jsonDoc.createNestedArray("params");
  _jsonDoc["connected"] = false;
  _jsonDoc["timestamp"] = 0;

  if (_fatalError) {
    _setState(WIFI_ERROR);
  }
}

bool DataServerClass::update() {
  // These must run every loop iteration for responsive networking, so they
  // sit ahead of the update-interval throttle below.
  _wsServer.loop();
  _server.handleClient();

  unsigned long now = millis();
  if (now - _lastModuleUpdate < _updateInterval) {
    return false;
  }
  _lastModuleUpdate = now;

  // A setup fault (no AP / no filesystem) is unrecoverable: stay in WIFI_ERROR
  // so the status LED keeps blinking red instead of reverting to a normal state.
  if (_fatalError) {
    return true;
  }

  WiFiState prevState = _state;
  if (hasClients()) {
    _setState(WIFI_CONNECTED);
    if (prevState != WIFI_CONNECTED) {
      Log.infof("WiFi client connected (%d total)", getClientCount());
    }
  } else {
    _setState(WIFI_UNCONNECTED);
    if (prevState == WIFI_CONNECTED) {
      Log.info("WiFi client disconnected");
    }
  }

  // Push the latest snapshot to any connected WebSocket clients. Copy the
  // JSON out under the lock (the ECU task may be rewriting it on core 0), then
  // broadcast outside the lock so network I/O doesn't stall the ECU task.
  String snapshot;
  bool doSend = false;
  if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY);
  if (_dataChanged && _wsServer.connectedClients() > 0) {
    snapshot = _lastJsonData;
    _dataChanged = false;
    doSend = true;
  }
  if (_dataMutex) xSemaphoreGive(_dataMutex);
  if (doSend) _wsServer.broadcastTXT(snapshot);

  return true;
}

void DataServerClass::sendData(const char* paramName, int rawValue, float convertedValue, const char* unit) {
  if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY);
  bool found = false;
  for (JsonObject obj : _dataArray) {
    if (obj["name"] == paramName) {
      obj["raw"] = rawValue;
      obj["value"] = convertedValue;
      obj["unit"] = unit;
      obj["time"] = millis();
      found = true;
      break;
    }
  }
  
  if (!found) {
    JsonObject newParam = _dataArray.createNestedObject();
    newParam["name"] = paramName;
    newParam["raw"] = rawValue;
    newParam["value"] = convertedValue;
    newParam["unit"] = unit;
    newParam["time"] = millis();
  }
  
  _jsonDoc["timestamp"] = millis();
  _jsonDoc["connected"] = true;
  
  _lastJsonData = "";
  serializeJson(_jsonDoc, _lastJsonData);
  _lastDataTime = millis();
  _dataChanged = true;
  if (_dataMutex) xSemaphoreGive(_dataMutex);
}

void DataServerClass::sendDTC(const char* type, const char* code, const char* description) {
  if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY);
  if (!_jsonDoc.containsKey("dtc")) {
    _jsonDoc.createNestedArray("dtc");
  }
  
  JsonArray dtcArray = _jsonDoc["dtc"];
  JsonObject dtc = dtcArray.createNestedObject();
  dtc["type"] = type;
  dtc["code"] = code;
  dtc["desc"] = description;

  _lastJsonData = "";
  serializeJson(_jsonDoc, _lastJsonData);
  _dataChanged = true;
  if (_dataMutex) xSemaphoreGive(_dataMutex);
}

void DataServerClass::sendStatus(const char* message) {
  if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY);
  _jsonDoc["status"] = message;
  _lastJsonData = "";
  serializeJson(_jsonDoc, _lastJsonData);
  _dataChanged = true;
  if (_dataMutex) xSemaphoreGive(_dataMutex);
}

int DataServerClass::getClientCount() {
  // Devices associated to the SoftAP (joined the WiFi network), not WebSocket
  // clients. This is the signal that drives WIFI_CONNECTED, so the status LED
  // and ECU polling react when a phone joins the network, before the app opens.
  return WiFi.softAPgetStationNum();
}

bool DataServerClass::hasClients() {
  return getClientCount() > 0;
}

const char* DataServerClass::getIPAddress() {
  return _ipAddress.c_str();
}

void DataServerClass::_onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      IPAddress rip = _wsServer.remoteIP(num);
      Log.infof("WebSocket client #%u connected from %s", num, rip.toString().c_str());
      // Send the current snapshot immediately so the UI populates on connect.
      String snapshot;
      if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY);
      snapshot = _lastJsonData;
      if (_dataMutex) xSemaphoreGive(_dataMutex);
      _wsServer.sendTXT(num, snapshot);
      break;
    }
    case WStype_DISCONNECTED:
      Log.infof("WebSocket client #%u disconnected", num);
      break;
    default:
      // TEXT/BIN/PING/PONG etc. — the app is receive-only for now.
      break;
  }
}

//=============================================================================
// Private Methods
//=============================================================================

void DataServerClass::_handleRoot() {
  // The PWA's entry point, served from LittleFS (uploaded via `uploadfs`).
  _sendFile("/index.html");
}

void DataServerClass::_handleData() {
  String snapshot;
  if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY);
  snapshot = _lastJsonData;
  if (_dataMutex) xSemaphoreGive(_dataMutex);
  _server.send(200, "application/json", snapshot);
}

void DataServerClass::_handleNotFound() {
  // Static-file fallback: any request not matched by an API route is treated as
  // a request for a PWA asset in LittleFS (css/js/icons/manifest/etc.).
  String uri = _server.uri();
  if (LittleFS.exists(uri)) {
    _sendFile(uri);
    return;
  }
  _server.send(404, "text/plain", "Not found");
}

const char* DataServerClass::_contentType(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".ico"))  return "image/x-icon";
  return "text/plain";
}

void DataServerClass::_sendFile(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    _server.send(404, "text/plain", "Not found");
    return;
  }
  _server.streamFile(f, _contentType(path));
  f.close();
}

void DataServerClass::_handleCors() {
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  _server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  _server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  _server.send(204);
}

void DataServerClass::_handleLog() {
  String json = "{\"ram\":\"" + Log.getRAMLog() + "\",\"ramUsed\":" + String(Log.getRAMUsed()) + "}";
  json.replace("\n", "\\n");
  _server.send(200, "application/json", json);
}

void DataServerClass::_handleLogClear() {
  Log.clearRAM();
  _server.send(200, "application/json", "{\"status\":\"ram_cleared\"}");
}

void DataServerClass::_handleLogRAM() {
  String log = Log.getRAMLog();
  _server.send(200, "text/plain", log);
}

void DataServerClass::_handleLogFlash() {
  String log = Log.getFlashLog();
  _server.send(200, "text/plain", log);
}

void DataServerClass::_handleLogFlashClear() {
  Log.clearFlash();
  _server.send(200, "application/json", "{\"status\":\"flash_cleared\"}");
}

void DataServerClass::_handlePollPost() {
  // Runtime (non-persisted) active poll set: the app sends exactly the params
  // shown on the current page; everything else is turned off.
  _server.sendHeader("Access-Control-Allow-Origin", "*");

  if (!_server.hasArg("plain")) {
    _server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No body\"}");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, _server.arg("plain"))) {
    _server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Bad JSON\"}");
    return;
  }

  ECU.clearPollFactors();
  int n = 0;
  for (JsonVariant v : doc["params"].as<JsonArray>()) {
    // Accept either "Name" (factor 1) or { "name":..., "factor":N }.
    const char* name = nullptr;
    int factor = 1;
    if (v.is<JsonObject>()) {
      name = v["name"].as<const char*>();
      factor = v["factor"] | 1;
    } else {
      name = v.as<const char*>();
    }
    if (name && factor > 0) { ECU.setPollFactor(name, factor); n++; }
  }

  String resp = "{\"status\":\"ok\",\"active\":" + String(n) + "}";
  _server.send(200, "application/json", resp);
}

void DataServerClass::_handleParams() {
  // Full catalog for the active ECU: every param with its address, current
  // scale/offset (default 1.0/0.0 if never configured) and live poll factor.
  JsonDocument doc;
  doc["ecu"] = ECU_ROM_ID;
  doc["name"] = ECU_NAME;
  doc["readMs"] = ECU.getReadMs();

  JsonArray arr = doc["params"].to<JsonArray>();
  int n = sizeof(ecuParams) / sizeof(ecuParams[0]);
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = ecuParams[i].name;

    char addr[8];
    snprintf(addr, sizeof(addr), "0x%04X", (unsigned)ecuParams[i].addr);
    o["addr"] = addr;

    // Hardcoded formula (fixed, read-only) + user calibration (editable) + factor.
    ParamMapping map = getParamMapping(ecuParams[i].name);
    o["unit"]       = map.unit;
    o["hw_scale"]   = map.scale;
    o["hw_offset"]  = map.offset;
    o["cal_slope"]  = ScalingConfig.getMultiplier(ecuParams[i].name);
    o["cal_offset"] = ScalingConfig.getOffset(ecuParams[i].name);
    o["factor"]     = ECU.getPollFactor(ecuParams[i].name);
  }

  String json;
  serializeJson(doc, json);
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  _server.send(200, "application/json", json);
}

void DataServerClass::_handleScalingGet() {
  JsonDocument doc;
  ScalingConfig.toJson(doc);
  doc["status"] = "ok";
  
  String json;
  serializeJson(doc, json);
  
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  _server.send(200, "application/json", json);
}

void DataServerClass::_handleScalingPost() {
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  
  if (!_server.hasArg("plain")) {
    _server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No body\"}");
    return;
  }
  
  String body = _server.arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  
  if (err) {
    String errMsg = "{\"status\":\"error\",\"message\":\"JSON parse error: ";
    errMsg += err.c_str();
    errMsg += "\"}";
    _server.send(400, "application/json", errMsg);
    return;
  }
  
  // Apply the scaling factors
  if (!ScalingConfig.fromJson(doc)) {
    _server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid scaling data\"}");
    return;
  }
  
  // Save to flash
  if (!ScalingConfig.save()) {
    _server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save to flash\"}");
    return;
  }

  // Push any updated poll factors to the live scheduler so they take effect
  // immediately (no reboot). Calibration slope/offset are applied by the app.
  int np = sizeof(ecuParams) / sizeof(ecuParams[0]);
  for (int i = 0; i < np; i++) {
    int f = ScalingConfig.getFactor(ecuParams[i].name);
    if (f >= 0) ECU.setPollFactor(ecuParams[i].name, f);
  }

  // Read back and verify
  JsonDocument verifyDoc;
  ScalingConfig.toJson(verifyDoc);
  verifyDoc["status"] = "saved";
  verifyDoc["message"] = "Scaling factors saved to EEPROM";
  
  String response;
  serializeJson(verifyDoc, response);
  _server.send(200, "application/json", response);
  
  Log.info("Scaling config saved via HTTP");
}
