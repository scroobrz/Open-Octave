#ifndef FIRMWARE_V5_CONFIG_H
#define FIRMWARE_V5_CONFIG_H

#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <stdint.h>

#define SERIAL_BUF_SIZE 64

#define WIFI_SSID "Open Octave"
#define WIFI_PASSWORD "oop321321"

#define ESP32_IP      IPAddress(192, 168, 4, 100)
#define ESP32_GATEWAY IPAddress(192, 168, 4, 1)
#define ESP32_SUBNET  IPAddress(255, 255, 255, 0)

#define CONTROLLER_IP "192.168.4.2" // replace with real IP
#define CONTROLLER_PORT 81

#define WIFI_CHECK_INTERVAL 20000

#define HEARTBEAT_INTERVAL 1000
#define HEARTBEAT_TIMEOUT 2000
#define CHAIN_HEARTBEAT_BYTE 0xFE

#define MAX_SEQUENCE_LENGTH 64
#define MAX_KEYS_PER_STEP 4

#define MIN_NOTE_DURATION 100
#define MAX_NOTE_DURATION 10000

// ============ HARDWARE CONFIG ============

#define DEBOUNCE_DELAY 50
#define SERVO_RELEASE_DELAY 120   // ms to wait for servo to physically release

#define SERVO_FREQ 50
#define SERVO_REST_ANGLE 0
#define SERVO_PRESS_ANGLE 180
#define SERVO_MIN_SAFE_ANGLE 0
#define SERVO_MAX_SAFE_ANGLE 180

#define NUM_KEYS 12
#define MAX_MODULES 4
#define MAX_TOTAL_KEYS (NUM_KEYS * MAX_MODULES)

#define LED_STRIP_PIN 19
#define LED_BRIGHTNESS 50

// Pin assignments
// STRICTLY AVOID On ESP32: IO6-IO11 (SPI flash), IO1/IO3 (UART), IO34-IO39 (input-only)
// RESERVED On ESP32: IO21 (SDA) and IO22 (SCL) for I2C to PCA9685 and PCA9555

#define RX1 16
#define TX1 17

#define RX2 4
#define TX2 15

#define SPEAKER_PIN 25          // D2 (IO25) — DAC-capable, good for audio

#define KEY0_BUTTON_PIN 0
#define KEY0_LED_INDEX 0
#define KEY0_SERVO_CHANNEL 1
#define KEY0_NOTE 262  // C4

#define KEY1_BUTTON_PIN 1
#define KEY1_LED_INDEX 1
#define KEY1_SERVO_CHANNEL 2
#define KEY1_NOTE 277  // C#4

#define KEY2_BUTTON_PIN 2
#define KEY2_LED_INDEX 2
#define KEY2_SERVO_CHANNEL 3
#define KEY2_NOTE 294  // D4

#define KEY3_BUTTON_PIN 3
#define KEY3_LED_INDEX 3
#define KEY3_SERVO_CHANNEL 4
#define KEY3_NOTE 311  // D#4

#define KEY4_BUTTON_PIN 4
#define KEY4_LED_INDEX 4
#define KEY4_SERVO_CHANNEL 5
#define KEY4_NOTE 330  // E4

#define KEY5_BUTTON_PIN 5
#define KEY5_LED_INDEX 5
#define KEY5_SERVO_CHANNEL 6
#define KEY5_NOTE 349  // F4

#define KEY6_BUTTON_PIN 6
#define KEY6_LED_INDEX 6
#define KEY6_SERVO_CHANNEL 7
#define KEY6_NOTE 370  // F#4

#define KEY7_BUTTON_PIN 7
#define KEY7_LED_INDEX 7
#define KEY7_SERVO_CHANNEL 8
#define KEY7_NOTE 392  // G4

#define KEY8_BUTTON_PIN 8
#define KEY8_LED_INDEX 8
#define KEY8_SERVO_CHANNEL 9
#define KEY8_NOTE 415  // G#4

#define KEY9_BUTTON_PIN 9
#define KEY9_LED_INDEX 9
#define KEY9_SERVO_CHANNEL 10
#define KEY9_NOTE 440  // A4

#define KEY10_BUTTON_PIN 10
#define KEY10_LED_INDEX 10
#define KEY10_SERVO_CHANNEL 11
#define KEY10_NOTE 466 // A#4

#define KEY11_BUTTON_PIN 11
#define KEY11_LED_INDEX 11
#define KEY11_SERVO_CHANNEL 12
#define KEY11_NOTE 494 // B4

// ROYGBIV colour palette
#define COLOR_RED     0xFF0000
#define COLOR_ORANGE  0xFF8000
#define COLOR_YELLOW  0xFFFF00
#define COLOR_GREEN   0x00FF00
#define COLOR_BLUE    0x0000FF
#define COLOR_INDIGO  0x4B0082
#define COLOR_VIOLET  0x8000FF
#define COLOR_WHITE   0xFFFFFF

// Open Octave brand gradient (finger-to-colour mapping)
#define COLOR_CYAN    0x00B4D8
// Green defined above in ROYGBIV palette
#define COLOR_GOLD    0xFFD700
#define COLOR_CORAL   0xFF6B35
#define COLOR_MAGENTA 0xE8368F

// ============ TYPE DEFINITIONS ============

enum SequenceMode {
  GUIDED,         // LEDs light up in sequence, user must press key to advance
  TEACHING        // LEDs + servos play automatically
};

struct Key {
  int buttonPin;
  int ledIndex;
  int servoChannel;
  int noteFreq;
  bool isPressed;
};

struct SequenceStep {
  uint8_t numKeys;                       // Number of keys to activate
  uint8_t keys[MAX_KEYS_PER_STEP];       // Which keys to activate
  uint32_t colors[MAX_KEYS_PER_STEP];    // LED color
  uint16_t duration;                     // How long to hold (ms)
};

struct Sequence {
  int id;
  SequenceStep steps[MAX_SEQUENCE_LENGTH];
  uint8_t length;
  char name[32];
};

enum TestLogErrorCode : uint8_t {
  TESTLOG_OK = 0,
  TESTLOG_INVALID_STEP_INDEX = 1,
  TESTLOG_INVALID_KEY_INDEX = 2
};

#endif