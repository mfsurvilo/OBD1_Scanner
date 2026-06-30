#include "wifi_server.h"
#include "logger.h"
#include "scaling_config.h"
#include <WiFi.h>

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
  , _server(HTTP_PORT)
  , _lastJsonData("{}")
  , _ipAddress("")
  , _lastDataTime(0)
  , _updateInterval(100)
  , _lastModuleUpdate(0)
{
}

void DataServerClass::setup(unsigned long updateInterval) {
  _updateInterval = updateInterval;
  _lastModuleUpdate = 0;
  
  Serial.println("=== WiFi AP Setup ===");
  
  WiFi.mode(WIFI_AP);
  bool success = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  
  if (success) {
    Serial.println("WiFi AP started successfully!");
  } else {
    Serial.println("ERROR: WiFi AP failed to start!");
  }
  
  IPAddress ip = WiFi.softAPIP();
  _ipAddress = ip.toString();
  
  Log.setupf("WiFi AP: %s / %s", AP_SSID, _ipAddress.c_str());
  
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("IP Address: ");
  Serial.println(_ipAddress);
  Serial.print("HTTP Port: ");
  Serial.println(HTTP_PORT);
  Serial.println("======================");

  _server.on("/", HTTP_GET, [this]() { _handleRoot(); });
  _server.on("/data", HTTP_GET, [this]() { _handleData(); });
  _server.on("/data", HTTP_OPTIONS, [this]() { _handleCors(); });
  _server.on("/log", HTTP_GET, [this]() { _handleLog(); });
  _server.on("/log/ram", HTTP_GET, [this]() { _handleLogRAM(); });
  _server.on("/log/ram/clear", HTTP_GET, [this]() { _handleLogClear(); });
  _server.on("/log/flash", HTTP_GET, [this]() { _handleLogFlash(); });
  _server.on("/log/flash/clear", HTTP_GET, [this]() { _handleLogFlashClear(); });
  _server.on("/scaling", HTTP_GET, [this]() { _handleScalingGet(); });
  _server.on("/scaling", HTTP_POST, [this]() { _handleScalingPost(); });
  _server.on("/scaling", HTTP_OPTIONS, [this]() { _handleCors(); });
  _server.onNotFound([this]() { _handleNotFound(); });
  
  _server.enableCORS(true);
  _server.begin();
  Serial.println("HTTP server started");
  
  _jsonDoc.clear();
  _dataArray = _jsonDoc.createNestedArray("params");
  _jsonDoc["connected"] = false;
  _jsonDoc["timestamp"] = 0;
}

bool DataServerClass::update() {
  unsigned long now = millis();
  if (now - _lastModuleUpdate < _updateInterval) {
    return false;
  }
  _lastModuleUpdate = now;
  
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
  
  _server.handleClient();
  return true;
}

void DataServerClass::sendData(const char* paramName, int rawValue, float convertedValue, const char* unit) {
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
}

void DataServerClass::sendDTC(const char* type, const char* code, const char* description) {
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
}

void DataServerClass::sendStatus(const char* message) {
  _jsonDoc["status"] = message;
  _lastJsonData = "";
  serializeJson(_jsonDoc, _lastJsonData);
}

int DataServerClass::getClientCount() {
  return WiFi.softAPgetStationNum();
}

bool DataServerClass::hasClients() {
  return WiFi.softAPgetStationNum() > 0;
}

const char* DataServerClass::getIPAddress() {
  return _ipAddress.c_str();
}

//=============================================================================
// Private Methods
//=============================================================================

void DataServerClass::_handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
  <title>OBD1 Scanner</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: monospace; background: #1a1a2e; color: #0f0; padding: 20px; }
    h1 { color: #0ff; }
    pre { background: #16213e; padding: 10px; border-radius: 5px; overflow-x: auto; }
    .data { margin: 10px 0; }
  </style>
</head>
<body>
  <h1>Subaru SSM1 OBD1 Scanner</h1>
  <div class="data">
    <h2>Live Data</h2>
    <pre id="data">Waiting for data...</pre>
  </div>
  <script>
    function fetchData() {
      fetch('/data')
        .then(r => r.json())
        .then(d => {
          document.getElementById('data').textContent = JSON.stringify(d, null, 2);
        })
        .catch(e => console.error(e));
    }
    setInterval(fetchData, 500);
    fetchData();
  </script>
</body>
</html>
)";
  _server.send(200, "text/html", html);
}

void DataServerClass::_handleData() {
  _server.send(200, "application/json", _lastJsonData);
}

void DataServerClass::_handleNotFound() {
  _server.send(404, "text/plain", "Not found");
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
