#include "ECU.h"
#include "ecu_defs.h"
#include "pin_defs.h"
#include "logger.h"
#include "LED.h"
#include "wifi_server.h"
#include "scaling_config.h"

#ifdef SIMULATE_ECU
#include "simulation_data.h"
static const int SIM_ECU_RESPONSE_MS = 85;
#endif

//=============================================================================
// ECU Class Implementation
//=============================================================================

// Global instance
ECUClass ECU;

//=============================================================================
// Public Methods
//=============================================================================

ECUClass::ECUClass()
  : _state(ECU_NOT_CONNECTED)
  , _numParams(sizeof(ecuParams) / sizeof(ecuParams[0]))
  , _pollMutex(nullptr)
  , _lastRetryTime(0)
  , _updateInterval(10)
  , _lastModuleUpdate(0)
  , _readState(READ_IDLE)
  , _currentParamIndex(0)
  , _pendingAddr(0)
  , _readStartTime(0)
#ifdef SIMULATE_ECU
  , _simFrameIndex(0)
  , _lastSimAdvance(0)
#endif
{
  for (int i = 0; i < 32; i++) {
    _pollFactor[i] = 0;
    _credit[i] = 0;
  }
  _romIdBuffer[0] = _romIdBuffer[1] = _romIdBuffer[2] = 0;
  _responseBuffer[0] = _responseBuffer[1] = _responseBuffer[2] = 0;
}

void ECUClass::setup(unsigned long updateInterval) {
  _updateInterval = updateInterval;
  _lastModuleUpdate = 0;

  if (!_pollMutex) _pollMutex = xSemaphoreCreateMutex();

  // Startup: nothing is polled. The app turns on exactly the params shown on the
  // page it is viewing via POST /poll, so the ECU link only spends time on what
  // is on screen.
  for (int i = 0; i < 32; i++) {
    _pollFactor[i] = 0;
    _credit[i] = 0;
  }

  _ecuInit();
  
#ifdef SIMULATE_ECU
  logInfo("Simulation mode - delaying 4s...");
  unsigned long simStart = millis();
  while (millis() - simStart < 4000) {
    LED.update();
    delay(10);
  }
#else
  logInfo("Connecting to ECU...");
#endif
}

bool ECUClass::update() {
  unsigned long now = millis();
  if (now - _lastModuleUpdate < _updateInterval) {
    return false;
  }
  _lastModuleUpdate = now;

  // Only touch the K-line when a WiFi client is attached. A client is required
  // both for the live view and for recording (recording is browser-side over
  // the WebSocket, so it can't run without a connected client). With no client
  // there is nothing to serve, and the blocking connect handshake would only
  // stall the main loop and freeze the status-LED breathe.
  if (DataServer.getState() != WIFI_CONNECTED) {
    return true;
  }

  if (_state != ECU_CONNECTED) {
    _retryConnection();
    return true;
  }

  _readFromECU();
  return true;
}

int ECUClass::getReadMs() const {
#ifdef SIMULATE_ECU
  return SIM_ECU_RESPONSE_MS;
#else
  return READ_STOP_DELAY + READ_CMD_DELAY + 50;  // rough per-read cost on the real bus
#endif
}

void ECUClass::readTroubleCodes() {
  logDebug("Reading Trouble Codes...");
  
  if (troubleCodes.activeAddr[0] == 0 && troubleCodes.activeAddr[1] == 0) {
    logWarn("DTC addresses not defined for this ECU");
    return;
  }
  
  bool anyActive = false;
  bool anyStored = false;
  
  for (int byteNum = 0; byteNum < 3; byteNum++) {
    if (troubleCodes.activeAddr[byteNum] == 0) continue;
    
    int val = _ecuRead(troubleCodes.activeAddr[byteNum]);
    if (val < 0) continue;
    
    for (int bit = 0; bit < 8; bit++) {
      if (val & (1 << bit)) {
        int nameIdx = byteNum * 8 + bit;
        if (nameIdx < 24 && dtcNames[nameIdx] != NULL) {
          Log.warnf("DTC ACTIVE: %s", dtcNames[nameIdx]);
          anyActive = true;
        }
      }
    }
  }
  
  if (!anyActive) {
    logInfo("No active DTCs");
  }
  
  for (int byteNum = 0; byteNum < 3; byteNum++) {
    if (troubleCodes.storedAddr[byteNum] == 0) continue;
    
    int val = _ecuRead(troubleCodes.storedAddr[byteNum]);
    if (val < 0) continue;
    
    for (int bit = 0; bit < 8; bit++) {
      if (val & (1 << bit)) {
        int nameIdx = byteNum * 8 + bit;
        if (nameIdx < 24 && dtcNames[nameIdx] != NULL) {
          Log.infof("DTC STORED: %s", dtcNames[nameIdx]);
          anyStored = true;
        }
      }
    }
  }
  
  if (!anyStored) {
    logDebug("No stored DTCs");
  }
}

void ECUClass::clearTroubleCodes() {
  logInfo("Clearing Trouble Codes...");
  
  if (troubleCodes.clearAddr == 0) {
    logWarn("Clear address not defined for this ECU");
    return;
  }
  
  for (int i = 0; i < 4; i++) {
    if (_ecuWrite(troubleCodes.clearAddr, troubleCodes.clearValue)) {
      Log.debugf("Clear attempt %d/4 sent", i + 1);
    } else {
      Log.warnf("Clear attempt %d/4 FAILED", i + 1);
    }
    delay(180);
  }
  
  logInfo("DTCs cleared - turn off ignition to finalize");
  delay(500);
  readTroubleCodes();
}

//=============================================================================
// Private Methods
//=============================================================================

bool ECUClass::_verifyRomId() {
  const char* REQUIRED_ROM = "7232A5";
  
#ifdef SIMULATE_ECU
  Log.setupf("SIMULATION MODE - ROM: %s, ECU: %s", ECU_ROM_ID, ECU_NAME);
  return true;
#endif
  
  logDebug("Pulling ROM ID...");
  
  if (!_ecuGetRomId(_romIdBuffer)) {
    logError("Failed to read ROM ID - check connections");
    return false;
  }
  
  char receivedId[7];
  sprintf(receivedId, "%02X%02X%02X", _romIdBuffer[0], _romIdBuffer[1], _romIdBuffer[2]);
  
  Log.setupf("ROM ID: %s", receivedId);
  
  String received = String(receivedId);
  String required = String(REQUIRED_ROM);
  received.toUpperCase();
  required.toUpperCase();
  
  if (received == required) {
    logInfo("ECU connected successfully");
    return true;
  } else {
    Log.errorf("Unsupported ROM: %s (need %s)", receivedId, REQUIRED_ROM);
    _setState(ECU_ERROR);
    return false;
  }
}

void ECUClass::_retryConnection() {
  if (millis() - _lastRetryTime >= RETRY_INTERVAL || _lastRetryTime == 0) {
    _lastRetryTime = millis();
    
    if (_verifyRomId()) {
      _setState(ECU_CONNECTED);
      Log.infof("Reading %d parameters...", _numParams);
      readTroubleCodes();
    } else {
      Log.debugf("Retrying in %lu seconds...", RETRY_INTERVAL / 1000);
    }
  }
}

void ECUClass::_readFromECU() {
  unsigned long now = millis();
  
#ifdef SIMULATE_ECU
  // Simulation: non-blocking state machine
  switch (_readState) {
    case READ_IDLE: {
      int idx = _pickNextParam();
      if (idx < 0) return;   // nothing active - stay idle
      _currentParamIndex = idx;
      _pendingAddr = ecuParams[idx].addr;
      _readStartTime = now;
      _readState = READ_WAITING_RESPONSE;
      return;
    }

    case READ_WAITING_RESPONSE: {
      if (now - _readStartTime >= SIM_ECU_RESPONSE_MS) {
        // Advance simulation frame if needed
        if (now - _lastSimAdvance >= SIM_FRAME_INTERVAL) {
          _lastSimAdvance = now;
          _simFrameIndex = (_simFrameIndex + 1) % SIM_FRAMES;
        }
        
        // Serve the captured sample value verbatim - no random noise.
        int result = simData[_simFrameIndex][_currentParamIndex];

        // Apply the hardcoded formula: engineering = raw*scale + offset.
        ParamMapping map = getParamMapping(ecuParams[_currentParamIndex].name);
        float eng = result * map.scale + map.offset;
        DataServer.sendData(ecuParams[_currentParamIndex].name, result, eng, map.unit);
        _readState = READ_IDLE;
      }
      break;
    }

    default:
      _readState = READ_IDLE;
      break;
  }
  
#else
  // Real ECU: non-blocking state machine
  switch (_readState) {
    case READ_IDLE: {
      int idx = _pickNextParam();
      if (idx < 0) return;   // nothing active - stay idle
      _currentParamIndex = idx;
      _pendingAddr = ecuParams[idx].addr;

      // Send stop command
      byte txbuf[4] = {0x12, 0x00, 0x00, 0x00};
      Serial2.write(txbuf, 4);
      _readStartTime = now;
      _readState = READ_STOP_SENT;
      return;
    }
    
    case READ_STOP_SENT: {
      if (now - _readStartTime >= READ_STOP_DELAY) {
        // Clear buffer and send read command
        while (Serial2.read() >= 0);
        
        byte txbuf[4] = {0x78, (byte)(_pendingAddr >> 8), (byte)(_pendingAddr & 0xFF), 0x00};
        Serial2.write(txbuf, 4);
        Serial2.flush();
        _readStartTime = now;
        _readState = READ_CMD_SENT;
      }
      break;
    }
    
    case READ_CMD_SENT: {
      if (now - _readStartTime >= READ_CMD_DELAY) {
        _readStartTime = now;
        _readState = READ_WAITING_RESPONSE;
      }
      break;
    }
    
    case READ_WAITING_RESPONSE: {
      int available = Serial2.available();
      
      if (available >= 3) {
        // Data ready - read and process
        char response[3];
        Serial2.readBytes(response, 3);
        
        int result = -1;
        if (response[0] == (char)((_pendingAddr >> 8) & 0xFF) && 
            response[1] == (char)(_pendingAddr & 0xFF)) {
          result = (byte)response[2];
        }
        
        if (result >= 0) {
          // Apply the hardcoded formula: engineering = raw*scale + offset.
          ParamMapping map = getParamMapping(ecuParams[_currentParamIndex].name);
          float eng = result * map.scale + map.offset;
          DataServer.sendData(ecuParams[_currentParamIndex].name, result, eng, map.unit);
        } else {
          Log.warnf("%s read failed: %d", ecuParams[_currentParamIndex].name, result);
        }

        _readState = READ_IDLE;

      } else if (now - _readStartTime >= READ_TIMEOUT) {
        // Timeout
        Log.warnf("%s read timeout", ecuParams[_currentParamIndex].name);
        _readState = READ_IDLE;
      }
      // Otherwise, continue waiting (non-blocking)
      break;
    }
  }
#endif
}

//=============================================================================
// Polling Scheduler (weighted round-robin)
//=============================================================================

// Picks the next parameter to sample. A param with factor N is chosen N times
// as often as a factor-1 param, via a smooth weighted round-robin: each call we
// add every active param's factor to its credit, take the highest credit, then
// subtract the total factor sum from the winner. Returns -1 if nothing active.
int ECUClass::_pickNextParam() {
  if (_pollMutex) xSemaphoreTake(_pollMutex, portMAX_DELAY);

  int total = 0;
  for (int i = 0; i < _numParams; i++) total += _pollFactor[i];
  if (total <= 0) {
    if (_pollMutex) xSemaphoreGive(_pollMutex);
    return -1;
  }

  int best = -1;
  for (int i = 0; i < _numParams; i++) {
    if (_pollFactor[i] <= 0) continue;
    _credit[i] += _pollFactor[i];
    if (best < 0 || _credit[i] > _credit[best]) best = i;
  }
  _credit[best] -= total;

  if (_pollMutex) xSemaphoreGive(_pollMutex);
  return best;
}

int ECUClass::_findParamIndex(const char* name) const {
  for (int i = 0; i < _numParams; i++) {
    if (strcmp(ecuParams[i].name, name) == 0) return i;
  }
  return -1;
}

void ECUClass::setPollFactor(const char* name, int factor) {
  if (factor < 0) factor = 0;
  int idx = _findParamIndex(name);
  if (idx < 0) return;
  if (_pollMutex) xSemaphoreTake(_pollMutex, portMAX_DELAY);
  _pollFactor[idx] = factor;
  _credit[idx] = 0;   // reset accumulator so the new weight applies cleanly
  if (_pollMutex) xSemaphoreGive(_pollMutex);
}

int ECUClass::getPollFactor(const char* name) const {
  int idx = _findParamIndex(name);
  if (idx < 0) return 0;
  if (_pollMutex) xSemaphoreTake(_pollMutex, portMAX_DELAY);
  int f = _pollFactor[idx];
  if (_pollMutex) xSemaphoreGive(_pollMutex);
  return f;
}

void ECUClass::clearPollFactors() {
  if (_pollMutex) xSemaphoreTake(_pollMutex, portMAX_DELAY);
  for (int i = 0; i < _numParams; i++) {
    _pollFactor[i] = 0;
    _credit[i] = 0;
  }
  if (_pollMutex) xSemaphoreGive(_pollMutex);
}

//=============================================================================
// Low-Level ECU Communication
//=============================================================================

void ECUClass::_ecuInit() {
#ifdef SIMULATE_ECU
  // Nothing to init - simulation replays captured data from simulation_data.h.
#else
  Serial2.begin(ECU_BAUD, SERIAL_8E1, ECU_RX_PIN, ECU_TX_PIN);
  Serial2.setTimeout(300);
#endif
}

void ECUClass::_ecuStop() {
#ifndef SIMULATE_ECU
  byte txbuf[4] = {0x12, 0x00, 0x00, 0x00};
  Serial2.write(txbuf, 4);
  delay(50);
  Serial2.flush();
#endif
}

int ECUClass::_ecuRead(unsigned int addr) {
#ifdef SIMULATE_ECU
  delay(SIM_ECU_RESPONSE_MS);
  
  if (millis() - _lastSimAdvance >= SIM_FRAME_INTERVAL) {
    _lastSimAdvance = millis();
    _simFrameIndex = (_simFrameIndex + 1) % SIM_FRAMES;
  }
  
  for (int i = 0; i < _numParams; i++) {
    if (ecuParams[i].addr == addr) {
      return simData[_simFrameIndex][i];  // verbatim captured value
    }
  }
  
  for (int i = 0; i < 3; i++) {
    if (addr == troubleCodes.activeAddr[i]) return simActiveDTC[i];
    if (addr == troubleCodes.storedAddr[i]) return simStoredDTC[i];
  }
  
  return 128;
#else
  byte txbuf[4] = {0x78, (byte)(addr >> 8), (byte)(addr & 0xFF), 0x00};
  
  _ecuStop();
  while (Serial2.read() >= 0);
  
  Serial2.write(txbuf, 4);
  delay(50);
  Serial2.flush();
  
  char response[3] = {0};
  int num = Serial2.readBytes(response, 3);
  
  if (num != 3) return -1;
  if (response[0] == (char)((addr >> 8) & 0xFF) && 
      response[1] == (char)(addr & 0xFF)) {
    return (byte)response[2];
  }
  return -2;
#endif
}

bool ECUClass::_ecuWrite(unsigned int addr, byte value) {
#ifdef SIMULATE_ECU
  delay(SIM_ECU_RESPONSE_MS);
  return true;
#else
  byte txbuf[4] = {0xAA, (byte)(addr >> 8), (byte)(addr & 0xFF), value};
  
  _ecuStop();
  while (Serial2.read() >= 0);
  Serial2.write(txbuf, 4);
  delay(50);
  Serial2.flush();
  
  return true;
#endif
}

bool ECUClass::_ecuGetRomId(byte* buffer) {
#ifdef SIMULATE_ECU
  delay(SIM_ECU_RESPONSE_MS * 3);
  
  const char* romId = ECU_ROM_ID;
  auto hexChar = [](char c) -> byte {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
  };
  
  int len = strlen(romId);
  if (len >= 6) {
    buffer[0] = (hexChar(romId[0]) << 4) | hexChar(romId[1]);
    buffer[1] = (hexChar(romId[2]) << 4) | hexChar(romId[3]);
    buffer[2] = (hexChar(romId[4]) << 4) | hexChar(romId[5]);
  } else if (len >= 4) {
    buffer[0] = (hexChar(romId[0]) << 4) | hexChar(romId[1]);
    buffer[1] = (hexChar(romId[2]) << 4) | hexChar(romId[3]);
    buffer[2] = 0x00;
  } else {
    buffer[0] = buffer[1] = buffer[2] = 0x00;
    return false;
  }
  return true;
#else
  char romidCmd[4] = {0x00, 0x46, 0x48, 0x49};
  char romid[3] = {0};
  
  _ecuStop();
  while (Serial2.read() >= 0);
  _ecuRead(0x1337);  // Wake up
  
  for (int retries = 0; retries < 8; retries++) {
    Serial2.write(romidCmd, 4);
    int nbytes = Serial2.readBytes(romid, 3);
    if ((nbytes == 3) && (romid[0] != 0x00)) break;
  }
  
  buffer[0] = romid[0];
  buffer[1] = romid[1];
  buffer[2] = romid[2];
  
  return (romid[0] != 0x00);
#endif
}
