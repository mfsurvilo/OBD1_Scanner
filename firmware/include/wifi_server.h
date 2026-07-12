#ifndef WIFI_DATA_SERVER_H
#define WIFI_DATA_SERVER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "scaling_config.h"

//=============================================================================
// WiFi State
//=============================================================================
enum WiFiState {
  WIFI_UNCONNECTED,   // No clients attached
  WIFI_CONNECTED,     // Client(s) attached
  WIFI_ERROR          // Error condition
};

//=============================================================================
// WiFi Access Point and Data Server Class
//=============================================================================

class DataServerClass {
public:
  // Configuration constants
  static constexpr const char* AP_SSID = "OBD1_Scanner";
  static constexpr const char* AP_PASSWORD = "subaru92";
  static constexpr int AP_CHANNEL = 1;
  static constexpr int AP_MAX_CONNECTIONS = 4;
  static constexpr int HTTP_PORT = 80;
  static constexpr int WEBSOCKET_PORT = 81;
  static constexpr const char* MDNS_HOSTNAME = "obd1";  // reachable as obd1.local

  DataServerClass();
  
  void setup(unsigned long updateInterval = 100);
  bool update();
  
  void sendData(const char* paramName, int rawValue, float convertedValue, const char* unit);
  void sendDTC(const char* type, const char* code, const char* description);
  void sendStatus(const char* message);
  
  WiFiState getState() const { return _state; }
  int getClientCount();
  bool hasClients();
  const char* getIPAddress();

private:
  void _setState(WiFiState state) { _state = state; }
  void _handleRoot();
  void _handleData();
  void _handleNotFound();
  void _handleCors();
  void _handleLog();
  void _handleLogClear();
  void _handleLogRAM();
  void _handleLogFlash();
  void _handleLogFlashClear();
  void _handleParams();
  void _handlePollPost();
  void _handleScalingGet();
  void _handleScalingPost();
  void _onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  void _sendFile(const String& path);          // stream a LittleFS file to the client
  const char* _contentType(const String& path); // MIME type from file extension

  WiFiState _state;
  bool _fatalError;    // latched: AP or filesystem failed at setup, device unusable
  WebServer _server;
  WebSocketsServer _wsServer;
  String _lastJsonData;
  String _ipAddress;
  unsigned long _lastDataTime;
  unsigned long _updateInterval;
  unsigned long _lastModuleUpdate;
  JsonDocument _jsonDoc;
  JsonArray _dataArray;
  bool _dataChanged;   // new data awaiting WebSocket broadcast

  // Guards _jsonDoc/_dataArray/_lastJsonData/_dataChanged: the ECU task (core 0)
  // writes them via sendData() while the main loop (core 1) serializes/broadcasts
  // them. Without this, a String realloc mid-read would crash.
  SemaphoreHandle_t _dataMutex;
};

// Global instance
extern DataServerClass DataServer;

#endif // WIFI_DATA_SERVER_H
