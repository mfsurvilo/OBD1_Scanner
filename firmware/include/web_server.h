#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

//=============================================================================
// WebServerApp — barebones AP + HTTP/WebSocket server for the framework.
//
//  - Hosts the "OBD1_Scanner" WiFi access point; the phone joins it directly
//    (AP-only, works with no router). Reachable at 192.168.4.1 or obd1.local.
//  - Serves the PWA from LittleFS (uploaded via `pio run -t uploadfs`).
//  - GET  /status              -> JSON device status (version, uptime, heap...).
//  - POST /update/firmware     -> OTA the app image      (Update U_FLASH).
//  - POST /update/filesystem   -> OTA the PWA / LittleFS  (Update U_SPIFFS).
//    Both reboot on success. Field name for the uploaded file: "file".
//  - POST /wifi                 -> save home-Wi-Fi creds (STA) to NVS + connect.
//  - GET  /update/check         -> compare running version vs latest release.
//  - POST /update/pull          -> over the internet (STA), download firmware.bin
//    and filesystem.bin from the latest GitHub release and flash both, then
//    reboot. This is the one-button consumer update path.
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
  static constexpr const char* GH_REPO = "mfsurvilo/OBD1_Scanner";  // update source

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
  void   handleWifiPost();              // save STA creds -> NVS, connect
  void   handleUpdateCheck();           // latest release vs running version
  void   handleUpdatePull();            // download+flash fw + fs from the internet
  void   startSta();                    // begin STA with saved creds (non-block)
  bool   fetchLatestTag(String& tag);   // GitHub API: latest release tag_name
  bool   pullFile(const String& url, int command, String& err);  // https .bin -> flash
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

  String           _staSsid;         // saved home-Wi-Fi SSID ("" = not set)
};

extern WebServerClass WebServerApp;

#endif // WEB_SERVER_H
