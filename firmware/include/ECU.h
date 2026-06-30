#ifndef ECU_H
#define ECU_H

#include <Arduino.h>

//=============================================================================
// ECU State
//=============================================================================
enum ECUState {
  ECU_NOT_CONNECTED,
  ECU_CONNECTED,
  ECU_ERROR
};

// Async read states for non-blocking operation
enum ECUReadState {
  READ_IDLE,
  READ_STOP_SENT,
  READ_CMD_SENT,
  READ_WAITING_RESPONSE
};

//=============================================================================
// ECU Communication Class
//=============================================================================

class ECUClass {
public:
  static constexpr unsigned long RETRY_INTERVAL = 5000;
  static constexpr unsigned long CYCLE_DELAY = 10;
  static constexpr unsigned long READ_STOP_DELAY = 10;    // ms after stop cmd
  static constexpr unsigned long READ_CMD_DELAY = 50;     // ms after read cmd
  static constexpr unsigned long READ_TIMEOUT = 300;      // ms max wait for response
  
  ECUClass();
  
  void setup(unsigned long updateInterval = 10);
  bool update();
  
  ECUState getState() const { return _state; }
  
  void readTroubleCodes();
  void clearTroubleCodes();

private:
  void _setState(ECUState state) { _state = state; }
  bool _verifyRomId();
  void _retryConnection();
  void _readFromECU();
  
  // Low-level ECU communication
  void _ecuInit();
  void _ecuStop();
  int _ecuRead(unsigned int addr);
  bool _ecuWrite(unsigned int addr, byte value);
  bool _ecuGetRomId(byte* buffer);
  
  ECUState _state;
  int _numParams;
  unsigned long _lastPollTime[32];
  unsigned long _lastRetryTime;
  byte _romIdBuffer[3];
  unsigned long _updateInterval;
  unsigned long _lastModuleUpdate;
  
  // Async read state machine
  ECUReadState _readState;
  int _currentParamIndex;
  unsigned int _pendingAddr;
  unsigned long _readStartTime;
  char _responseBuffer[3];
  
#ifdef SIMULATE_ECU
  int _simFrameIndex;
  unsigned long _lastSimAdvance;
#endif
};

// Global instance
extern ECUClass ECU;

#endif // ECU_H
