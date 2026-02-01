/*
 * FIRMWARE V2
 */

#include "PCA9685.h"
#include <Adafruit_NeoPixel.h>
#include <Wire.h>

#define SPEAKER_PIN 2
#define MODE_SWITCH_PIN 3

#define SERVO_FREQ 50
#define SERVO_REST_ANGLE 0
#define SERVO_PRESS_ANGLE 90

#define NUM_LEDS 10
#define NUM_KEYS 3
#define DEBOUNCE_DELAY 50
#define SEQUENCE_LENGTH 5

#define KEY0_BUTTON_PIN 4
#define KEY0_LED_PIN 5
#define KEY0_SERVO_CHANNEL 0
#define KEY0_NOTE 262

#define KEY1_BUTTON_PIN 6
#define KEY1_LED_PIN 7
#define KEY1_SERVO_CHANNEL 1
#define KEY1_NOTE 294

#define KEY2_BUTTON_PIN 8
#define KEY2_LED_PIN 9
#define KEY2_SERVO_CHANNEL 2
#define KEY2_NOTE 330

#define COLOR_RED 0xFF0000
#define COLOR_GREEN 0x00FF00
#define COLOR_BLUE 0x0000FF
#define COLOR_WHITE 0xFFFFFF

#define LED_STICK(pin) Adafruit_NeoPixel(NUM_LEDS, pin, NEO_GRB + NEO_KHZ800)
#define autoPressKey(keyIndex)                                                 \
  servoDriver.setAngle(keys[keyIndex].servoChannel, SERVO_PRESS_ANGLE)
#define autoReleaseKey(keyIndex)                                               \
  servoDriver.setAngle(keys[keyIndex].servoChannel, SERVO_REST_ANGLE)

enum Mode {
  MANUAL,         // no automatic functions
  AUTOMATIC_LEDS, // system plays LED sequences automatically
  FULL_AUTOMATIC  // system plays LED and servo sequences automatically
};

struct Key {
  int buttonPin;              // digital pin for the button
  Adafruit_NeoPixel ledStick; // controller for the LED stick
  int servoChannel;           // channel on PCA9685 servo driver (0-15)
  int noteFreq;               // sound frequency in Hz
  bool isPressed;
};

struct SequenceStep {
  int keyIndex;
  int color;
  int duration;
};

Key keys[NUM_KEYS] = {
    {KEY0_BUTTON_PIN, LED_STICK(KEY0_LED_PIN), KEY0_SERVO_CHANNEL, KEY0_NOTE,
     false}, // C4
    {KEY1_BUTTON_PIN, LED_STICK(KEY1_LED_PIN), KEY1_SERVO_CHANNEL, KEY1_NOTE,
     false}, // D4
    {KEY2_BUTTON_PIN, LED_STICK(KEY2_LED_PIN), KEY2_SERVO_CHANNEL, KEY2_NOTE,
     false} // E4
};

const SequenceStep sequence[SEQUENCE_LENGTH] = {
    // {keyIndex, color, duration}
    {0, COLOR_BLUE, 500},
    {1, COLOR_GREEN, 500},
    {0, COLOR_BLUE, 500},
    {1, COLOR_GREEN, 500},
    {2, COLOR_RED, 500}};

ServoDriver servoDriver;

// global state tracking
Mode currentMode = MANUAL;
unsigned long lastModeSwitchTime = 0;
unsigned long lastKeyPressTime[NUM_KEYS] = {0};
int activeAudioKey = -1;
bool sequenceRunning = false;
int currentSequenceStep = 0;
unsigned long currentStepStartTime = 0;

// function forward-declarations
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

/*
===============================
Core arduino control functions
===============================
*/

void setup() {
  pinMode(MODE_SWITCH_PIN, INPUT);
  pinMode(SPEAKER_PIN, OUTPUT);

  Wire.begin();
  servoDriver.init();

  for (int i = 0; i < NUM_KEYS; i++) {
    pinMode(keys[i].buttonPin, INPUT);
    keys[i].ledStick.begin();
    keys[i].ledStick.show();
    keys[i].isPressed = false;
    servoDriver.setAngle(keys[i].servoChannel, SERVO_REST_ANGLE);
  }
}

void loop() {
  checkModeSwitch();
  checkButtons();

  if (currentMode == AUTOMATIC_LEDS || currentMode == FULL_AUTOMATIC) {
    handleAutomaticModes();
  }
}

/*
===============================
Mode control functions
===============================
*/

void checkModeSwitch() {
  if (millis() - lastModeSwitchTime >= DEBOUNCE_DELAY) {
    if (digitalRead(MODE_SWITCH_PIN) == HIGH) {
      switch (currentMode) {
      case MANUAL:
        setMode(AUTOMATIC_LEDS);
        startSequence();
        break;
      case AUTOMATIC_LEDS:
        setMode(FULL_AUTOMATIC);
        startSequence();
        break;
      case FULL_AUTOMATIC:
        setMode(MANUAL);
        break;
      }

      lastModeSwitchTime = millis();
    }
  }
}

void setMode(Mode mode) {
  for (int i = 0; i < NUM_KEYS; i++) {
    resetKey(i);
  }

  sequenceRunning = false;
  currentMode = mode;
}

void handleAutomaticModes() {
  if (!sequenceRunning)
    return;

  if (millis() - currentStepStartTime >= sequence[currentSequenceStep].duration) {
    resetKey(sequence[currentSequenceStep].keyIndex);

    currentSequenceStep++;

    if (currentSequenceStep >= SEQUENCE_LENGTH) {
      stopSequence();
      return;
    }

    executeSequenceStep(sequence[currentSequenceStep]);
  }
}

/*
===============================
Sequence control functions
===============================
*/

void startSequence() {
  sequenceRunning = true;
  currentSequenceStep = 0;
  currentStepStartTime = millis();

  executeSequenceStep(sequence[currentSequenceStep]);
}

void stopSequence() {
  for (int i = 0; i < NUM_KEYS; i++) {
    resetKey(i);
  }

  sequenceRunning = false;
}

void executeSequenceStep(const SequenceStep &step) {
  lightUpKey(step.keyIndex, step.color);

  if (currentMode == FULL_AUTOMATIC) {
    autoPressKey(step.keyIndex);
  }

  currentStepStartTime = millis();
}

/*
===============================
Keyboard control functions
===============================
*/

void checkButtons() {
  for (int i = 0; i < NUM_KEYS; i++) {
    bool buttonPressed = digitalRead(keys[i].buttonPin) == HIGH;

    if (buttonPressed && !keys[i].isPressed) {
      if (millis() - lastKeyPressTime[i] >= DEBOUNCE_DELAY) {
        keys[i].isPressed = true;
        lastKeyPressTime[i] = millis();
        startKeyTone(i);
      }
    } else if (!buttonPressed && keys[i].isPressed) {
      keys[i].isPressed = false;
      stopKeyTone(i);
    }
  }
}

void startKeyTone(int keyIndex) {
  activeAudioKey = keyIndex;
  tone(SPEAKER_PIN, keys[keyIndex].noteFreq);
}

void stopKeyTone(int keyIndex) {
  for (int i = 0; i < NUM_KEYS; i++) {
    if (keys[i].isPressed) {
      startKeyTone(i);
      return;
    }
  }

  activeAudioKey = -1;
  noTone(SPEAKER_PIN);
}

void lightUpKey(int keyIndex, int color) {
  Adafruit_NeoPixel &ledStick = keys[keyIndex].ledStick;

  for (int i = 0; i < NUM_LEDS; i++) {
    ledStick.setPixelColor(i, color);
  }

  ledStick.show();
}

void lightDownKey(int keyIndex) {
  Adafruit_NeoPixel &ledStick = keys[keyIndex].ledStick;

  for (int i = 0; i < NUM_LEDS; i++) {
    ledStick.setPixelColor(i, 0);
  }

  ledStick.show();
}

void resetKey(int keyIndex) {
  lightDownKey(keyIndex);
  autoReleaseKey(keyIndex);
}