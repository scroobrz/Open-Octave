#ifndef FIRMWARE_V3_CONFIG_H
#define FIRMWARE_V3_CONFIG_H

#include <Adafruit_NeoPixel.h>
#include <stdint.h>

// ============ HARDWARE CONFIG ============

#define DEBOUNCE_DELAY 50
#define SEQUENCE_LENGTH 4

#define SPEAKER_PIN 8
#define MODE_SWITCH_PIN 2

#define SERVO_FREQ 50
#define SERVO_REST_ANGLE 0
#define SERVO_PRESS_ANGLE 90

#define NUM_KEYS 2

#define STRIP_DATA_PIN 6
#define LEDS_PER_KEY 10
#define NUM_LEDS (NUM_KEYS * LEDS_PER_KEY)
#define LED_BRIGHTNESS 50

#define KEY0_BUTTON_PIN 3
#define KEY0_SERVO_CHANNEL 1
#define KEY0_NOTE 262

#define KEY1_BUTTON_PIN 4
#define KEY1_SERVO_CHANNEL 2
#define KEY1_NOTE 294

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
  int buttonPin;              // which Arduino pin the button is on
  int servoChannel;           // which channel on the PCA9685 (0-15)
  int noteFreq;
  bool isPressed;
};

struct SequenceStep {
  int keyIndex;
  uint32_t color;
  int duration;
};

// ============ FUNCTION PROTOTYPES ============

void checkModeSwitch();
void setMode(Mode mode);
void handleAutomaticModes();
void startSequence();
void stopSequence();
void executeSequenceStep(const SequenceStep &step);
void stopKeyTone(int keyIndex);
void checkButtons();
void lightUpKey(int keyIndex, uint32_t color);
void lightDownKey(int keyIndex);
void resetKey(int keyIndex);

#endif