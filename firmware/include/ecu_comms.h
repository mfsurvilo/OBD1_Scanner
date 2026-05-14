#ifndef ECU_COMMS_H
#define ECU_COMMS_H

#include <Arduino.h>

//=============================================================================
// ECU Communication Interface
// Implemented by either ecu_comms.cpp (real) or ecu_sim.cpp (simulation)
//=============================================================================

// Initialize ECU communication (call in setup())
void ecu_init();

// Stop/reset ECU communication
void ECU_Stop();

// Read a byte from ECU memory address
// Returns value (0-255) on success, negative on error
int ecu_read(unsigned int addr);

// Write a byte to ECU memory address (for clearing codes)
// Returns true on success
bool ecu_write(unsigned int addr, byte value);

// Read the ECU ROM ID (3 bytes)
// Returns true if successful
boolean ECU_GetROMID(byte* buffer);

// Check if running in simulation mode
bool isSimulationMode();

#endif // ECU_COMMS_H
