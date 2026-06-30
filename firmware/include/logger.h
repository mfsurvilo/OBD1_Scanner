#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

//=============================================================================
// Logger System
// Three output buffers with different persistence levels:
//   - Serial: DEBUG and above (live output + setup messages persist)
//   - RAM:    INFO and above (circular buffer, lost on reboot)
//   - Flash:  ERROR + logInfoPersist only (LittleFS, survives reboot)
//
// All entries timestamped with millis from boot.
//=============================================================================

// Log levels
enum LogLevel {
  LEVEL_DEBUG = 0,
  LEVEL_INFO  = 1,
  LEVEL_SETUP = 2,  // Same as INFO but persists in serial buffer
  LEVEL_WARN  = 3,
  LEVEL_ERROR = 4,
  LEVEL_PERSIST = 5 // Same as INFO but goes to flash
};

// Buffer sizes
constexpr size_t SERIAL_SETUP_BUFFER_SIZE = 1024;  // Setup messages buffer
constexpr size_t RAM_BUFFER_SIZE = 8192;           // 8KB RAM circular buffer

class Logger {
public:
  Logger();
  
  // Initialize logger (call in setup before other modules)
  void begin();
  
  // Main logging functions
  void debug(const char* msg);
  void info(const char* msg);
  void setup(const char* msg);   // Persists in serial buffer for replay
  void warn(const char* msg);
  void error(const char* msg);   // Also writes to flash
  void persist(const char* msg); // Writes to flash (for startup info)
  
  // Formatted versions
  void debugf(const char* format, ...);
  void infof(const char* format, ...);
  void setupf(const char* format, ...);
  void warnf(const char* format, ...);
  void errorf(const char* format, ...);
  void persistf(const char* format, ...);
  
  // Buffer access (for HTTP endpoints)
  String getRAMLog();
  String getFlashLog();
  String getSetupLog();
  
  // Clear logs
  void clearRAM();
  void clearFlash();
  
  // Stats
  size_t getRAMUsed();
  size_t getFlashUsed();
  
  // Call periodically to flush setup messages to serial when it connects
  void update();

private:
  void _log(LogLevel level, const char* msg);
  void _writeSerial(LogLevel level, unsigned long timestamp, const char* msg);
  void _writeRAM(LogLevel level, unsigned long timestamp, const char* msg);
  void _writeFlash(LogLevel level, unsigned long timestamp, const char* msg);
  void _writeSetupBuffer(LogLevel level, unsigned long timestamp, const char* msg);
  const char* _levelToString(LogLevel level);
  
  // Serial setup buffer (replays when serial connects)
  char _setupBuffer[SERIAL_SETUP_BUFFER_SIZE];
  size_t _setupWritePos;
  bool _setupFlushed;
  
  // RAM circular buffer
  char _ramBuffer[RAM_BUFFER_SIZE];
  size_t _ramWritePos;
  bool _ramWrapped;
  
  // Flash logging state
  bool _flashReady;
  size_t _flashSize;
};

// Global instance
extern Logger Log;

// Convenience macros (match your API)
#define logDebug(msg)   Log.debug(msg)
#define logInfo(msg)    Log.info(msg)
#define logSetup(msg)   Log.setup(msg)
#define logWarn(msg)    Log.warn(msg)
#define logError(msg)   Log.error(msg)
#define logInfoPersist(msg) Log.persist(msg)

#endif // LOGGER_H
