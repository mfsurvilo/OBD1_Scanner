#include "logger.h"
#include <LittleFS.h>
#include <cstdarg>

//=============================================================================
// Logger Implementation
//=============================================================================

// Global instance
Logger Log;

// Flash log file path
static const char* FLASH_LOG_FILE = "/log.txt";

//=============================================================================
// Public Methods
//=============================================================================

Logger::Logger()
  : _setupWritePos(0)
  , _setupFlushed(false)
  , _ramWritePos(0)
  , _ramWrapped(false)
  , _flashReady(false)
  , _flashSize(0)
{
  memset(_setupBuffer, 0, SERIAL_SETUP_BUFFER_SIZE);
  memset(_ramBuffer, 0, RAM_BUFFER_SIZE);
}

void Logger::begin() {
  if (LittleFS.begin(true)) {
    _flashReady = true;
    
    if (LittleFS.exists(FLASH_LOG_FILE)) {
      File f = LittleFS.open(FLASH_LOG_FILE, "r");
      if (f) {
        _flashSize = f.size();
        f.close();
      }
    }
  } else {
    Serial.println("[LOG] ERROR: LittleFS mount failed!");
  }
}

void Logger::update() {
  if (Serial && !_setupFlushed && _setupWritePos > 0) {
    Serial.println();
    Serial.println("=== Setup Log ===");
    Serial.write(_setupBuffer, _setupWritePos);
    Serial.println("=================");
    _setupFlushed = true;
  }
}

void Logger::debug(const char* msg)   { _log(LEVEL_DEBUG, msg); }
void Logger::info(const char* msg)    { _log(LEVEL_INFO, msg); }
void Logger::setup(const char* msg)   { _log(LEVEL_SETUP, msg); }
void Logger::warn(const char* msg)    { _log(LEVEL_WARN, msg); }
void Logger::error(const char* msg)   { _log(LEVEL_ERROR, msg); }
void Logger::persist(const char* msg) { _log(LEVEL_PERSIST, msg); }

void Logger::debugf(const char* format, ...) {
  char buf[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  debug(buf);
}

void Logger::infof(const char* format, ...) {
  char buf[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  info(buf);
}

void Logger::setupf(const char* format, ...) {
  char buf[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  setup(buf);
}

void Logger::warnf(const char* format, ...) {
  char buf[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  warn(buf);
}

void Logger::errorf(const char* format, ...) {
  char buf[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  error(buf);
}

void Logger::persistf(const char* format, ...) {
  char buf[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  persist(buf);
}

String Logger::getRAMLog() {
  String result;
  result.reserve(RAM_BUFFER_SIZE);
  
  if (_ramWrapped) {
    for (size_t i = _ramWritePos; i < RAM_BUFFER_SIZE; i++) {
      if (_ramBuffer[i] != '\0') result += _ramBuffer[i];
    }
  }
  for (size_t i = 0; i < _ramWritePos; i++) {
    if (_ramBuffer[i] != '\0') result += _ramBuffer[i];
  }
  
  return result;
}

String Logger::getFlashLog() {
  if (!_flashReady) return "Flash not available";
  
  File f = LittleFS.open(FLASH_LOG_FILE, "r");
  if (!f) return "No log file";
  
  String result = f.readString();
  f.close();
  return result;
}

String Logger::getSetupLog() {
  return String(_setupBuffer);
}

void Logger::clearRAM() {
  memset(_ramBuffer, 0, RAM_BUFFER_SIZE);
  _ramWritePos = 0;
  _ramWrapped = false;
  info("RAM log cleared");
}

void Logger::clearFlash() {
  if (_flashReady && LittleFS.exists(FLASH_LOG_FILE)) {
    LittleFS.remove(FLASH_LOG_FILE);
    _flashSize = 0;
    info("Flash log cleared");
  }
}

size_t Logger::getRAMUsed() {
  return _ramWrapped ? RAM_BUFFER_SIZE : _ramWritePos;
}

size_t Logger::getFlashUsed() {
  return _flashSize;
}

//=============================================================================
// Private Methods
//=============================================================================

const char* Logger::_levelToString(LogLevel level) {
  switch (level) {
    case LEVEL_DEBUG:   return "DEBUG";
    case LEVEL_INFO:    return "INFO";
    case LEVEL_SETUP:   return "INFO";
    case LEVEL_WARN:    return "WARN";
    case LEVEL_ERROR:   return "ERROR";
    case LEVEL_PERSIST: return "INFO";
    default:            return "???";
  }
}

void Logger::_log(LogLevel level, const char* msg) {
  unsigned long timestamp = millis();
  
  if (level >= LEVEL_DEBUG) {
    _writeSerial(level, timestamp, msg);
  }
  
  if (level == LEVEL_SETUP) {
    _writeSetupBuffer(level, timestamp, msg);
  }
  
  if (level >= LEVEL_INFO && level != LEVEL_PERSIST) {
    _writeRAM(level, timestamp, msg);
  }
  
  if (level == LEVEL_ERROR || level == LEVEL_PERSIST) {
    _writeFlash(level, timestamp, msg);
  }
}

void Logger::_writeSerial(LogLevel level, unsigned long timestamp, const char* msg) {
  if (Serial) {
    Serial.printf("[%s] %s\n", _levelToString(level), msg);
  }
}

void Logger::_writeSetupBuffer(LogLevel level, unsigned long timestamp, const char* msg) {
  char entry[128];
  int len = snprintf(entry, sizeof(entry), "[%8lu][%s] %s\n", 
                     timestamp, _levelToString(level), msg);
  
  if (_setupWritePos + len < SERIAL_SETUP_BUFFER_SIZE) {
    memcpy(_setupBuffer + _setupWritePos, entry, len);
    _setupWritePos += len;
  }
}

void Logger::_writeRAM(LogLevel level, unsigned long timestamp, const char* msg) {
  char entry[128];
  int len = snprintf(entry, sizeof(entry), "[%8lu][%s] %s\n", 
                     timestamp, _levelToString(level), msg);
  if (len >= (int)sizeof(entry)) len = sizeof(entry) - 1;
  
  for (int i = 0; i < len; i++) {
    _ramBuffer[_ramWritePos] = entry[i];
    _ramWritePos = (_ramWritePos + 1) % RAM_BUFFER_SIZE;
    if (_ramWritePos == 0) {
      _ramWrapped = true;
    }
  }
}

void Logger::_writeFlash(LogLevel level, unsigned long timestamp, const char* msg) {
  if (!_flashReady) return;
  
  File f = LittleFS.open(FLASH_LOG_FILE, "a");
  if (f) {
    f.printf("[%8lu][%s] %s\n", timestamp, _levelToString(level), msg);
    _flashSize = f.size();
    f.close();
  }
}
