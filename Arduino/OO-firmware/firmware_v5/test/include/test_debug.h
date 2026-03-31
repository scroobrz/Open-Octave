#ifndef TEST_DEBUG_H
#define TEST_DEBUG_H

#include <iostream>
#include <cstdio>
#include <cstdarg>

// Simple logging macros for unit tests - portable version
// For unit tests, logging is disabled by default for cleaner output
// Uncomment the line below to enable debug logging:
#define ENABLE_TEST_LOGGING

#ifdef ENABLE_TEST_LOGGING
// Simple non-empty macros when logging enabled
#define LOGLN(msg) std::cout << msg << std::endl

// For LOGF, we'll make it a no-op wrapper since printf formatting is tricky cross-platform
// In real firmware, these would call the logging system
#define LOGF(fmt, ...) (void)0

// For when we DO need logging (debugging), use a simple implementation:
inline void logf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

#else
// Empty macros when logging disabled
#define LOGLN(msg) (void)0
#define LOGF(fmt, ...) (void)0
#endif

#endif // TEST_DEBUG_H
