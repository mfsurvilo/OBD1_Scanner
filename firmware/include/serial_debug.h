#ifndef SERIAL_DEBUG_H
#define SERIAL_DEBUG_H

#include <Arduino.h>

//=============================================================================
// Serial Debug Interface
// Non-blocking debug output - prints only if USB serial is connected
//=============================================================================

// Initialize serial debug (non-blocking)
void debug_init();

// Check if debug serial is available
bool debug_available();

// Print functions (only output if serial connected)
void debug_print(const char* msg);
void debug_print(const String& msg);
void debug_print(int val);
void debug_print(unsigned int val, int base = DEC);
void debug_println();
void debug_println(const char* msg);
void debug_println(const String& msg);
void debug_println(int val);

// Formatted printing
void debug_printf(const char* format, ...);

// Print a separator line
void debug_separator();

// Read serial command (returns empty string if none available)
String debug_readCommand();

#endif // SERIAL_DEBUG_H
