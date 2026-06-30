#ifndef WIFI_DATA_SERVER_H
#define WIFI_DATA_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "scaling_config.h"

//=============================================================================
// WiFi State
//=============================================================================
enum WiFiState {
  WIFI_UNCONNECTED,   // No clients attached
  WIFI_CONNECTED,     // Client(s) attached
  WIFI_FAULT          // Error condition
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
  void _handleScalingGet();
  void _handleScalingPost();
  
  WiFiState _state;
  WebServer _server;
  String _lastJsonData;
  String _ipAddress;
  unsigned long _lastDataTime;
  unsigned long _updateInterval;
  unsigned long _lastModuleUpdate;
  JsonDocument _jsonDoc;
  JsonArray _dataArray;
};

// Global instance
extern DataServerClass DataServer;

#endif // WIFI_DATA_SERVER_H
