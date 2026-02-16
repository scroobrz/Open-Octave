#ifndef FIRMWARE_V4_DEBUG_H
#define FIRMWARE_V4_DEBUG_H

#include <stdarg.h>

// ============ DEBUG CONFIGURATION ============

// Set to 1 to enable debug output, 0 to disable
#define DEBUG_ENABLED 1
#define SERIAL_BAUD_RATE 115200

#if DEBUG_ENABLED

// Initialize serial communication and wait for connection
#define LOG_INIT()                                                             \
  do {                                                                         \
    Serial.begin(SERIAL_BAUD_RATE);                                            \
    while (!Serial) {                                                          \
      delay(10);                                                               \
    }                                                                          \
  } while (0)

// Print string literals (automatically stored in flash)
#define LOG(str) Serial.print(F(str))
#define LOGLN(str) Serial.println(F(str))

// Print variables/values
#define LOG_VAL(x) Serial.print(x)
#define LOGLN_VAL(x) Serial.println(x)

// Lightweight printf that reads format string from flash
inline void LOGF_P(const char *str_P, ...) {
  va_list args;
  va_start(args, str_P);

  char c;
  while ((c = pgm_read_byte(str_P++))) {
    if (c == '%') {
      c = pgm_read_byte(str_P++);

      // Check for 'l' modifier (long)
      bool isLong = false;
      if (c == 'l') {
        isLong = true;
        c = pgm_read_byte(str_P++);
      }

      switch (c) {
      case 'd': // Signed decimal
      case 'i':
        if (isLong)
          Serial.print(va_arg(args, long));
        else
          Serial.print(va_arg(args, int));
        break;

      case 'u': // Unsigned decimal
        if (isLong)
          Serial.print(va_arg(args, unsigned long));
        else
          Serial.print(va_arg(args, unsigned int));
        break;

      case 'x': // Hex lowercase
      case 'X': // Hex uppercase
        if (isLong)
          Serial.print(va_arg(args, unsigned long), HEX);
        else
          Serial.print(va_arg(args, unsigned int), HEX);
        break;

      case 's': // String (from RAM)
        Serial.print(va_arg(args, const char *));
        break;

      case 'c': // Character
        Serial.print((char)va_arg(args, int));
        break;

      case '%': // Literal %
        Serial.print('%');
        break;

      default: // Unknown - print as-is
        Serial.print('%');
        if (isLong)
          Serial.print('l');
        Serial.print(c);
        break;
      }
    } else {
      Serial.print(c);
    }
  }

  va_end(args);
}

// Macro to automatically wrap format strings with PSTR() for Flash storage
#define LOGF(str, ...) LOGF_P(PSTR(str), ##__VA_ARGS__)

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