#ifndef FIRMWARE_V5_DEBUG_H
#define FIRMWARE_V5_DEBUG_H

#include <iostream>
#include <string>
#include <sstream>

#include "Arduino.h"

// Mock debug logging for unit tests
// These will output to std::cout instead of Serial

#define LOG(x) std::cout << x << std::flush
#define LOGLN(x) std::cout << x << "\n"
#define LOGF(...) { \
    char _buf[256]; \
    snprintf(_buf, sizeof(_buf), __VA_ARGS__); \
    std::cout << _buf << std::flush; \
}

// Debug level for unit tests (max verbosity for testing)
#define LOG_LEVEL 4  // 0=none, 1=error, 2=warn, 3=info, 4=debug

// For F() macro with PROGMEM strings (just forward to normal string)
#define F(s) s

// Arduino USB types for compatibility
typedef unsigned char uart_nr_t;

// HardwareSerial stubs (mocked)
class HardwareSerial {
public:
    HardwareSerial(int uart_nr) : _uart_nr(uart_nr) {}

    void begin(unsigned long baud) {}
    void begin(unsigned long baud, uint32_t mode) {}

    void println(const char* str) { std::cout << str << "\n"; }
    void println(const char* str, int mode) { std::cout << str << "\n"; }
    void print(const char* str) { std::cout << str; }
    void print(int val) { std::cout << val; }
    void print(unsigned long val) { std::cout << val; }
    void print(double val) { std::cout << val; }
    void print(const char* str, int) { std::cout << str; }

    void write(const char* str, size_t len) {
        std::cout.write(str, len);
    }
    void write(const char* str) {
        std::cout << str;
    }
    void write(char c) {
        std::cout << c;
    }

    void flush() { std::cout.flush(); }

    // Mock: create readable/writable property
    void setReadable(bool readable = true) { _readable = readable; }
    void setWritable(bool writable = true) { _writable = writable; }

    int available() { return _readable ? 1 : 0; }
    int read() {
        if (_readable && !_read_buffer.empty()) {
            char c = _read_buffer[0];
            _read_buffer.erase(0, 1);
            return c;
        }
        return -1;
    }

    void setReadBuffer(const std::string& buffer) {
        _read_buffer = buffer;
        _readable = true;
    }

    size_t availableForWrite() { return _writable ? 256 : 0; }

    // Lock/unlock for multi-threaded access (mocked for unit tests)
    void lockRxBuffer() {}
    void unlockRxBuffer() {}

private:
    int _uart_nr;
    bool _readable = false;
    bool _writable = true;
    std::string _read_buffer;
};

// Choose which stream to use for Serial - both go to std::cout
#define USE_STREAM_EVERYWHERE

#endif // FIRMWARE_V5_DEBUG_H
