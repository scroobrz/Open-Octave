#ifndef FIRMWARE_V4_CONFIG_H
#define FIRMWARE_V4_CONFIG_H

#include "PCA9685.h"
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <stdint.h>

// ============ HARDWARE CONFIG ============

#define DEBOUNCE_DELAY 50
#define SERVO_RELEASE_DELAY 50   // ms to wait for servo to physically release
#define MIN_NOTE_DURATION 100     // Minimum time (ms) a note will play when key is pressed

#define MAX_SEQUENCE_LENGTH 16
#define NUM_SEQUENCES 8

#define WIFI_SSID "Open Octave"
#define WIFI_PASSWORD "oop321321"

#define SPEAKER_PIN 25          // ESP32: safe DAC-capable pin

#define SERVO_FREQ 50
#define SERVO_REST_ANGLE 0
#define SERVO_PRESS_ANGLE 120
#define SERVO_MIN_SAFE_ANGLE 0
#define SERVO_MAX_SAFE_ANGLE 180

#define NUM_KEYS 3

#define LEDS_PER_KEY 1
#define LED_BRIGHTNESS 50

// ESP32 GPIO Pin Assignments (temporary — update for final wiring)
// AVOID: GPIO 6-11 (SPI flash), GPIO 1/3 (UART0), GPIO 34-39 (input-only)
// RESERVED: GPIO 21 (SDA) and GPIO 22 (SCL) for I2C to PCA9685
#define KEY0_BUTTON_PIN 4       // ESP32: safe general-purpose
#define KEY0_LED_PIN 16         // ESP32: safe general-purpose
#define KEY0_SERVO_CHANNEL 1
#define KEY0_NOTE 392

#define KEY1_BUTTON_PIN 5       // ESP32: safe general-purpose
#define KEY1_LED_PIN 17         // ESP32: safe general-purpose
#define KEY1_SERVO_CHANNEL 2
#define KEY1_NOTE 330

#define KEY2_BUTTON_PIN 18      // ESP32: safe general-purpose
#define KEY2_LED_PIN 19         // ESP32: safe general-purpose
#define KEY2_SERVO_CHANNEL 3
#define KEY2_NOTE 262

// ROYGBIV colour palette
#define COLOR_RED     0xFF0000
#define COLOR_ORANGE  0xFF8000
#define COLOR_YELLOW  0xFFFF00
#define COLOR_GREEN   0x00FF00
#define COLOR_BLUE    0x0000FF
#define COLOR_INDIGO  0x4B0082
#define COLOR_VIOLET  0x8000FF
#define COLOR_WHITE   0xFFFFFF

// ============ TYPE DEFINITIONS ============

enum Mode {
  MANUAL,         // no automatic functions, user plays manually
  GUIDED,         // LEDs light up in sequence, user must press key to advance
  TEACHING        // LEDs + servos play automatically
};

struct Key {
  int buttonPin;
  int ledPin;                  // Store pin number, NeoPixel created in setup()
  Adafruit_NeoPixel* led;      // Pointer to NeoPixel object (initialized in setup)
  int servoChannel;
  int noteFreq;
  bool isPressed;
};

struct SequenceStep {
  uint8_t keyIndex;   // Which key to activate (0 to NUM_KEYS-1)
  uint32_t color;     // LED color
  uint16_t duration;  // How long to hold (ms)
};

struct Sequence {
  const SequenceStep* steps;  // Pointer to step array
  uint8_t length;             // Number of steps in this sequence
  const char* name;           // Display name for logging
};

enum TestLogErrorCode : uint8_t {
  TESTLOG_OK = 0,
  TESTLOG_INVALID_STEP_INDEX = 1,
  TESTLOG_INVALID_KEY_INDEX = 2
};

#endif