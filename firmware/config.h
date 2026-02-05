#ifndef CONFIG_H
#define CONFIG_H

#include <Adafruit_NeoPixel.h>

#define SPEAKER_PIN 8
#define MODE_SWITCH_PIN 2

#define SERVO_FREQ 50
#define SERVO_REST_ANGLE 0
#define SERVO_PRESS_ANGLE 180

#define NUM_LEDS 10
#define NUM_KEYS 2
#define DEBOUNCE_DELAY 50
#define SEQUENCE_LENGTH 4

#define KEY0_BUTTON_PIN 3
#define KEY0_LED_PIN 5
#define KEY0_SERVO_CHANNEL 1
#define KEY0_NOTE 262

#define KEY1_BUTTON_PIN 4
#define KEY1_LED_PIN 6
#define KEY1_SERVO_CHANNEL 2
#define KEY1_NOTE 294

#define COLOR_RED 0xFF0000
#define COLOR_GREEN 0x00FF00
#define COLOR_BLUE 0x0000FF
#define COLOR_WHITE 0xFFFFFF

#define ledStick(pin) Adafruit_NeoPixel(NUM_LEDS, pin, NEO_GRB + NEO_KHZ800)
#define servoPull(channel) servoDriver.setAngle(channel, SERVO_PRESS_ANGLE)
#define servoRest(channel) servoDriver.setAngle(channel, SERVO_REST_ANGLE)
#define autoPressKey(keyIndex) servoPull(keys[keyIndex].servoChannel)
#define autoReleaseKey(keyIndex) servoRest(keys[keyIndex].servoChannel)

// ============ TYPE DEFINITIONS ============

enum Mode {
  MANUAL,         // no automatic functions, user plays manually
  AUTOMATIC_LEDS, // LEDs light up in sequence
  FULL_AUTOMATIC  // LEDs + servos play automatically
};

struct Key {
  int buttonPin;              // which Arduino pin the button is on
  Adafruit_NeoPixel ledStick; // controller for this key's LED stick
  int servoChannel;           // which channel on the PCA9685 motor driver (0-15)
  int noteFreq;
  bool isPressed;
};

struct SequenceStep {
  int keyIndex;
  int color;
  int duration;
};

// ============ FUNCTION PROTOTYPES ============

void checkModeSwitch();
void setMode(Mode mode);
void handleAutomaticModes();
void startSequence();
void stopSequence();
void executeSequenceStep(const SequenceStep &step);
void startKeyTone(int keyIndex);
void stopKeyTone(int keyIndex);
void checkButtons();
void lightUpKey(int keyIndex, int color);
void lightDownKey(int keyIndex);
void resetKey(int keyIndex);

#endif