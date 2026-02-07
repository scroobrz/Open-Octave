#ifndef FIRMWARE_V3_CONFIG_H
#define FIRMWARE_V3_CONFIG_H

#include "PCA9685.h" // controls the PCA9685 servo motor driver (I2C)
#include <Wire.h>    // allows I2C communication with the servo driver
#include <stdint.h>

// ============ HARDWARE CONFIG ============

#define DEBOUNCE_DELAY 50

#define MAX_SEQUENCE_LENGTH 16
#define NUM_SEQUENCES 3

#define SPEAKER_PIN 8

#define SERVO_FREQ 50
#define SERVO_REST_ANGLE 0
#define SERVO_PRESS_ANGLE 180
#define SERVO_MIN_SAFE_ANGLE 0
#define SERVO_MAX_SAFE_ANGLE 180

#define NUM_KEYS 3

#define LEDS_PER_KEY 1
#define LED_BRIGHTNESS 50

#define KEY0_BUTTON_PIN 2
#define KEY0_LED_PIN 5
#define KEY0_SERVO_CHANNEL 1
#define KEY0_NOTE 262

#define KEY1_BUTTON_PIN 3
#define KEY1_LED_PIN 6
#define KEY1_SERVO_CHANNEL 2
#define KEY1_NOTE 294

#define KEY2_BUTTON_PIN 4
#define KEY2_LED_PIN 7
#define KEY2_SERVO_CHANNEL 3
#define KEY2_NOTE 329

#define COLOR_RED 0xFF0000
#define COLOR_GREEN 0x00FF00
#define COLOR_BLUE 0x0000FF
#define COLOR_WHITE 0xFFFFFF

// ============ TYPE DEFINITIONS ============

enum Mode {
  MANUAL,         // no automatic functions, user plays manually
  AUTOMATIC_LEDS, // LEDs light up in sequence
  FULL_AUTOMATIC  // LEDs + servos play automatically
};

struct Key {
  int buttonPin;
  Adafruit_NeoPixel led;
  int servoChannel;
  int noteFreq;
  bool isPressed;
};

struct SequenceStep {
  uint8_t keyIndex;   // Which key to activate (0 to NUM_KEYS-1)
  uint32_t color;     // LED color
  uint16_t duration;  // How long to hold (ms)
};

// Sequence struct bundles steps with their length
struct Sequence {
  const SequenceStep* steps;  // Pointer to step array
  uint8_t length;             // Number of steps in this sequence
  const char* name;           // Display name for logging
};

// ============ HELPER MACROS ============

#define IS_VALID_KEY_INDEX(idx) ((idx) >= 0 && (idx) < NUM_KEYS)
#define LED(pin) Adafruit_NeoPixel(LEDS_PER_KEY, pin, NEO_GRB + NEO_KHZ800)
#define startKeyTone(keyIndex) tone(SPEAKER_PIN, keys[keyIndex].noteFreq)
#define servoPull(channel) safeServoSetAngle(channel, SERVO_PRESS_ANGLE)
#define servoRest(channel) safeServoSetAngle(channel, SERVO_REST_ANGLE)
#define autoPressKey(keyIndex) servoPull(keys[keyIndex].servoChannel)
#define autoReleaseKey(keyIndex) servoRest(keys[keyIndex].servoChannel)
#define currentSequence sequences[currentSequenceIndex]
#define currentSequenceStep sequences[currentSequenceIndex].steps[currentSequenceStepIndex]

// ============ FUNCTION PROTOTYPES ============

void processSerialCommands();
void setMode(Mode mode);
void handleAutomaticModes();
void startSequence();
void stopSequence();
void executeSequenceStep(const SequenceStep &step);
void selectSequence(int index);
void nextSequence();
void prevSequence();
void stopKeyTone(int keyIndex);
void checkButtons();
void lightUpKey(int keyIndex, uint32_t color);
void lightDownKey(int keyIndex);
void resetKey(int keyIndex);
void safeServoSetAngle(int servoChannel, int angle);
bool validateSequenceData();
bool validateHardwareInit();
void testLEDs();
void testServos();

#endif