#ifndef FIRMWARE_V4_DEBUG_H
#define FIRMWARE_V4_DEBUG_H

// #include <WebSerial.h>  // WebSerial disabled until AsyncWebServer migration

// ============ DEBUG CONFIGURATION ============

// Set to 1 to enable debug output, 0 to disable
#define DEBUG_ENABLED 1
#define SERIAL_BAUD_RATE 115200

#if DEBUG_ENABLED

// Initialize serial communication
#define LOG_INIT()                                                             \
  do {                                                                         \
    Serial.begin(SERIAL_BAUD_RATE);                                            \
    delay(100);                                                                \
  } while (0)

// Print string literals
#define LOG(str)   { Serial.print(F(str)); }
#define LOGLN(str) { Serial.println(F(str)); }

// Print variables/values
#define LOG_VAL(x)   { Serial.print(x); }
#define LOGLN_VAL(x) { Serial.println(x); }

// Print formatted strings
#define LOGF(str, ...) Serial.printf(str, ##__VA_ARGS__)

#else

// When DEBUG_ENABLED is 0, all logging macros become no-ops
#define LOG_INIT()
#define LOG(str)
#define LOGLN(str)
#define LOG_VAL(x)
#define LOGLN_VAL(x)
#define LOGF(...)

#endif
#endif