#ifndef WIFI_SERVER_H
#define WIFI_SERVER_H

#include <Arduino.h>

//=============================================================================
// WiFi Access Point and Data Server
// Creates a WiFi AP and serves ECU data via HTTP/WebSocket
//=============================================================================

namespace WiFiServer {

// WiFi AP Configuration
constexpr const char* AP_SSID = "OBD1_Scanner";
constexpr const char* AP_PASSWORD = "subaru92";  // 8+ chars required
constexpr int AP_CHANNEL = 1;
constexpr int AP_MAX_CONNECTIONS = 4;

// Server Configuration
constexpr int HTTP_PORT = 80;
constexpr int WS_PORT = 81;

// Initialize WiFi AP and start servers
void setup();

// Handle incoming connections (call from loop)
void update();

// Send ECU data to connected clients
void sendData(const char* paramName, int rawValue, float convertedValue, const char* unit);

// Send DTC data
void sendDTC(const char* type, const char* code, const char* description);

// Send status message
void sendStatus(const char* message);

// Get number of connected clients
int getClientCount();

// Check if any clients are connected
bool hasClients();

// Get AP IP address as string
const char* getIPAddress();

}

#endif // WIFI_SERVER_H
