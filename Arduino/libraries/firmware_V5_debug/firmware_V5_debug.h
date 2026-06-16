#ifndef FIRMWARE_V5_DEBUG_H
#define FIRMWARE_V5_DEBUG_H

// ============ DEBUG CONFIGURATION ============

// Set to 1 to enable debug output, 0 to disable
#define DEBUG_ENABLED 1

// Maximum length of a single formatted log message.
// Kept small for SRAM; longer messages will be truncated.
#define LOG_BUFFER_SIZE 128

#if DEBUG_ENABLED

// Forward declaration
void wsSendLog(const char* msg);

// Print string literals (uses F() for flash storage)
#define LOG(str)   do { Serial.print(F(str)); wsSendLog(str); } while(0)
#define LOGLN(str) do { Serial.println(F(str)); wsSendLog(str); wsSendLog("\n"); } while(0)

// Print variables/values (non-string-literal values)
// These use a small stack buffer to convert the value to a string,
// then send it to both Serial and WebSocket.
#define LOG_VAL(x)   do {                         \
    Serial.print(x);                              \
    char _buf[LOG_BUFFER_SIZE];                   \
    String _tmp(x);                               \
    _tmp.toCharArray(_buf, sizeof(_buf));          \
    wsSendLog(_buf);                          \
  } while(0)

#define LOGLN_VAL(x) do {                         \
    Serial.println(x);                            \
    char _buf[LOG_BUFFER_SIZE];                   \
    String _tmp(x);                               \
    _tmp.toCharArray(_buf, sizeof(_buf));          \
    wsSendLog(_buf);                          \
    wsSendLog("\n");                          \
  } while(0)

// Print formatted strings (printf-style)
// Uses a stack buffer so no heap allocation is needed.
#define LOGF(fmt, ...) do {                       \
    Serial.printf(fmt, ##__VA_ARGS__);            \
    char _buf[LOG_BUFFER_SIZE];                   \
    snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    wsSendLog(_buf);                          \
  } while(0)

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