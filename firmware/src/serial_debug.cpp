#include "serial_debug.h"
#include <stdarg.h>

//=============================================================================
// Serial Debug Implementation
// Non-blocking - outputs to USB serial only when connected
//=============================================================================

static String commandBuffer = "";

void debug_init() {
  Serial.begin(115200);
  // Don't wait for serial - just continue
}

bool debug_available() {
  return Serial;  // Returns true if USB serial is connected
}

void debug_print(const char* msg) {
  if (Serial) Serial.print(msg);
}

void debug_print(const String& msg) {
  if (Serial) Serial.print(msg);
}

void debug_print(int val) {
  if (Serial) Serial.print(val);
}

void debug_print(unsigned int val, int base) {
  if (Serial) Serial.print(val, base);
}

void debug_println() {
  if (Serial) Serial.println();
}

void debug_println(const char* msg) {
  if (Serial) Serial.println(msg);
}

void debug_println(const String& msg) {
  if (Serial) Serial.println(msg);
}

void debug_println(int val) {
  if (Serial) Serial.println(val);
}

void debug_printf(const char* format, ...) {
  if (!Serial) return;
  
  char buf[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  Serial.print(buf);
}

void debug_separator() {
  if (Serial) Serial.println("-------------------------------------------");
}

String debug_readCommand() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      String cmd = commandBuffer;
      cmd.trim();
      cmd.toUpperCase();
      commandBuffer = "";
      return cmd;
    } else {
      commandBuffer += c;
    }
  }
  return "";  // No complete command yet
}
