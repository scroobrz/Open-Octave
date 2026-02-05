#ifndef FIRMWARE_V3_CONFIG_H
#define FIRMWARE_V3_CONFIG_H

#include <stdint.h>
#include "PCA9685.h"           // controls the PCA9685 servo motor driver (I2C)
#include <Wire.h>              // allows I2C communication with the servo driver

// ============ HARDWARE CONFIG ============

#define DEBOUNCE_DELAY 50
#define SEQUENCE_LENGTH 4

#define SPEAKER_PIN 8

#define SERVO_FREQ 50
#define SERVO_REST_ANGLE 0
#define SERVO_PRESS_ANGLE 180

#define NUM_KEYS 2

#define LEDS_PER_KEY 1
#define LED_BRIGHTNESS 50

#define KEY0_BUTTON_PIN 3
#define KEY0_LED_PIN 6
#define KEY0_SERVO_CHANNEL 1
#define KEY0_NOTE 262

#define KEY1_BUTTON_PIN 4
#define KEY1_LED_PIN 7
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
  int buttonPin;             // which Arduino pin the button is on
  int ledPin;                // Arduino digital pin used as data (DI) for this WS2813 LED
  int servoChannel;
  int noteFreq;
  bool isPressed;
};

struct SequenceStep {
  int keyIndex;
  uint32_t color;
  int duration;
};

// ============ FUNCTION PROTOTYPES ============

void processSerialCommands();
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
void runLEDTest();

#endif