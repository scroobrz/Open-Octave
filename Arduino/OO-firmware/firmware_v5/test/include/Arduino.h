#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <cstdint>
#include <string>
#include <iostream>

// Arduino standard types
typedef uint8_t byte;
typedef unsigned long word;

// Arduino constants
#define HIGH 0x1
#define LOW 0x0

#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2

#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

#define SERIAL 0x0
#define DISPLAY 0x1

#define LSBFIRST 0
#define MSBFIRST 1

#define CHANGE 1
#define FALLING 2
#define RISING 3

// Extended Arduino types
typedef uint16_t uint16_t;
typedef uint32_t uint32_t;
typedef uint64_t uint64_t;
typedef int16_t int16_t;
typedef int32_t int32_t;
typedef int64_t int64_t;

// Standard Arduino functions to be mocked
extern unsigned long millis();
extern unsigned long micros();
extern void delay(unsigned long ms);
extern void delayMicroseconds(unsigned int us);

// Mock utilities for testing
extern void mock_millis_set_time(unsigned long time);

extern void pinMode(uint8_t pin, uint8_t mode);
extern void digitalWrite(uint8_t pin, uint8_t val);
extern int digitalRead(uint8_t pin);
extern int analogRead(uint8_t pin);
extern void analogReference(uint8_t mode);
extern void analogWrite(uint8_t pin, int val);

extern void attachInterrupt(uint8_t interruptNum, void (*userFunc)(void), int mode);
extern void detachInterrupt(uint8_t interruptNum);

extern uint8_t lowByte(uint16_t val);
extern uint8_t highByte(uint16_t val);

// Serial printing mocks
extern std::ostream& Serial;

// Tone/noTone mocks
extern void tone(uint8_t pin, unsigned int frequency);
extern void tone(uint8_t pin, unsigned int frequency, unsigned long duration);
extern void noTone(uint8_t pin);

// Random number mocks
extern long random(long max);
extern long random(long min, long max);
extern void randomSeed(unsigned long seed);

// Bit manipulation
extern long map(long x, long in_min, long in_max, long out_min, long out_max);

#endif // MOCK_ARDUINO_H
