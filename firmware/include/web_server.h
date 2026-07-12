#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "ota_container.h"

//=============================================================================
// WebServerApp — barebones AP + HTTP/WebSocket server for the framework.
//
//  - Hosts the "OBD1_Scanner" WiFi access point; the phone joins it directly
//    (AP-only, works with no router). Reachable at 192.168.4.1 or obd1.local.
//  - Serves the PWA from LittleFS (uploaded via `pio run -t uploadfs`).
//  - GET  /status              -> JSON device status (version, uptime, heap...).
//  - POST /update/firmware     -> OTA the app image      (Update U_FLASH).
//  - POST /update/filesystem   -> OTA the PWA / LittleFS  (Update U_SPIFFS).
//  - POST /update/combined     -> OTA both from one .ota container (below).
//    All reboot on success. Field name for the uploaded file: "file".
//
// .ota container (little-endian, streamable): 16-byte header
//     magic[4]="OB1U", u8 version, u8 flags, u16 reserved,
//     u32 fw_len, u32 fs_len
// followed by fw_len bytes (app image) then fs_len bytes (LittleFS image).
//  - WebSocket on :81 broadcasts the status JSON ~1 Hz as a heartbeat.
//
// OTA is intentionally unauthenticated for now (AP is WPA2-protected). Add a
// token check in the /update handlers when the framework matures.
//=============================================================================
class WebServerClass {
public:
  static constexpr const char* AP_SSID     = "OBD1_Scanner";
  static constexpr const char* AP_PASSWORD = "subaru92";  // >= 8 chars (WPA2)
  static constexpr int  AP_CHANNEL     = 1;
  static constexpr int  AP_MAX_CLIENTS = 4;
  static constexpr int  HTTP_PORT      = 80;
  static constexpr int  WS_PORT        = 81;
  static constexpr const char* MDNS_HOST = "obd1";        // http://obd1.local

  void begin();
  void loop();

  int  clientCount();
  const char* ip() const { return _ip.c_str(); }
  bool ok() const { return _ok; }   // AP + filesystem both up (health signal)

private:
  void   handleStatus();
  void   streamOr404();                 // serve a LittleFS file (SPA fallback)
  void   handleUpload(int command);     // U_FLASH or U_SPIFFS chunk pump
  void   finishUpdate();                // reply + schedule reboot
  void   handleCombinedUpload();        // .ota container: fw + fs in one file
  void   finishCombined();              // reply + schedule reboot
  String statusJson();
  static const char* contentType(const String& path);

  WebServer        _http{HTTP_PORT};
  WebSocketsServer _ws{WS_PORT};
  String           _ip;
  bool             _ok           = false;
  bool             _fsMounted    = false;
  bool             _rebootPending = false;
  unsigned long    _rebootAt      = 0;
  unsigned long    _lastBroadcast = 0;

  ota::Parser _ota;                  // .ota streaming parser (pure state machine)
  bool     _otaIoError = false;      // an Update begin/write/end call failed
  bool     _otaFwCommitted = false;  // combined: app slot boot already switched
};

extern WebServerClass WebServerApp;

#endif // WEB_SERVER_H
