#include "firmware_v3_config.h"
#include "PCA9685.h"           // controls the PCA9685 servo motor driver (I2C)
#include <Adafruit_NeoPixel.h> // controls the LED sticks/strips
#include <Wire.h>              // allows I2C communication with the servo driver

/*
 * FIRMWARE V3
 *
 * This firmware currently controls 3 keys across the following three modes:
 *   - MANUAL: User plays keys manually, no automation
 *   - AUTOMATIC_LEDS: LEDs light up in a sequence
 *   - FULL_AUTOMATIC: LEDs + servos play automatically (no user input needed)
 *
 * Modes are cycled through by pressing the mode switch button.
 *
 * Sound is always triggered by button presses - whether the user presses a key
 * or a servo pulls it down, the button underneath is what triggers the sound.
 */

// ============ HELPER MACROS ============

#define startKeyTone(keyIndex) tone(SPEAKER_PIN, keys[keyIndex].noteFreq)
#define servoPull(channel) servoDriver.setAngle(channel, SERVO_PRESS_ANGLE)
#define servoRest(channel) servoDriver.setAngle(channel, SERVO_REST_ANGLE)
#define autoPressKey(keyIndex) servoPull(keys[keyIndex].servoChannel)
#define autoReleaseKey(keyIndex) servoRest(keys[keyIndex].servoChannel)
#define KEY_LED(pin) Adafruit_NeoPixel(LEDS_PER_KEY, pin, NEO_GRB + NEO_KHZ800)

// ============ HARDWARE & SEQUENCE DEFINITIONS ============

Key keys[NUM_KEYS] = {
  {KEY0_BUTTON_PIN, KEY0_LED_PIN, KEY_LED(KEY0_LED_PIN), KEY0_SERVO_CHANNEL, KEY0_NOTE, false}, //C4
  {KEY1_BUTTON_PIN, KEY1_LED_PIN, KEY_LED(KEY1_LED_PIN), KEY1_SERVO_CHANNEL, KEY1_NOTE, false}  //D4
};

const SequenceStep sequence[SEQUENCE_LENGTH] = {
    {0, COLOR_BLUE, 500},
    {1, COLOR_GREEN, 500},
    {0, COLOR_BLUE, 500},
    {1, COLOR_RED, 500}
};

ServoDriver servoDriver; // controls all servos via I2C

// ============ GLOBAL STATE ============

Mode currentMode = MANUAL;
unsigned long lastModeSwitchTime = 0; // when mode switch was last pressed (for debouncing)
bool previousModeSwitchState = LOW; // previous state of mode switch button for edge detection
unsigned long lastKeyPressTime[NUM_KEYS] = {0}; // when each key was last pressed (for debouncing)
bool sequenceRunning = false;
int currentSequenceStep = 0;
unsigned long currentStepStartTime = 0;

/*
===============================
     CORE ARDUINO FUNCTIONS
===============================
These are the two functions that Arduino calls automatically.
*/

// runs once
void setup() {
  pinMode(MODE_SWITCH_PIN, INPUT);
  pinMode(SPEAKER_PIN, OUTPUT);

  Wire.begin();
  servoDriver.init();
  servoDriver.setFrequency(SERVO_FREQ);

  noTone(SPEAKER_PIN);
  digitalWrite(SPEAKER_PIN, LOW);


  // initialize each key
  for (int i = 0; i < NUM_KEYS; i++) {
    pinMode(keys[i].buttonPin, INPUT);

    keys[i].led.begin();
    keys[i].led.setBrightness(120); // max brightness = 255
    keys[i].led.show();

    servoRest(keys[i].servoChannel);
    keys[i].isPressed = false;
  }

  runBootTest();
}

// runs repeatedly forever
void loop() {
  checkModeSwitch(); // see if user wants to change modes
  checkButtons();    // detect any key presses and play sounds

  // if we're in an automatic mode, handle the sequence playback
  if (currentMode == AUTOMATIC_LEDS || currentMode == FULL_AUTOMATIC) {
    handleAutomaticModes();
  }
}

/*
===============================
    MODE CONTROL FUNCTIONS
===============================
These handle switching between and handling the MANUAL, AUTOMATIC_LEDS, and FULL_AUTOMATIC modes.
*/

// checks if the mode switch button was pressed and cycles to the next mode
void checkModeSwitch() {
  bool currentModeSwitchState = (digitalRead(MODE_SWITCH_PIN) == HIGH);
  unsigned long now = millis();

  // Debounce on rising edge: only act when the button transitions from
  // not pressed to pressed, and sufficient time has passed since the
  // last accepted mode change.
  if (currentModeSwitchState && !previousModeSwitchState &&
      (now - lastModeSwitchTime >= DEBOUNCE_DELAY)) {

    // switch to the next mode
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

    lastModeSwitchTime = now;
  }

  // Remember state for next call so we can detect edges.
  previousModeSwitchState = currentModeSwitchState;
}

// switches to a new mode and resets everything to a clean state
void setMode(Mode mode) {
  for (int i = 0; i < NUM_KEYS; i++) {
    resetKey(i);
  }
  noTone(SPEAKER_PIN);

  sequenceRunning = false;
  currentMode = mode;
}

// handles automatic sequence playback
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
   SEQUENCE CONTROL FUNCTIONS
===============================
These handle starting, stopping, and playing automatic sequences.
*/

// starts playing the sequence from the beginning
void startSequence() {
  sequenceRunning = true;
  currentSequenceStep = 0;
  currentStepStartTime = millis();

  // immediately play the first step
  executeSequenceStep(sequence[currentSequenceStep]);
}

// stops the sequence and turns off all keys
void stopSequence() {
  for (int i = 0; i < NUM_KEYS; i++) {
    resetKey(i);
  }

  sequenceRunning = false;
}

// plays a single step of a sequence
void executeSequenceStep(const SequenceStep &step) {
  // light up the key's LED with the specified color
  lightUpKey(step.keyIndex, step.color);

  // if we're in full automatic mode, also press the key with the servo
  if (currentMode == FULL_AUTOMATIC) {
    autoPressKey(step.keyIndex);
  }

  currentStepStartTime = millis();
}

/*
===============================
   KEYBOARD CONTROL FUNCTIONS
===============================
These handle button detection, sound playback, and LED control for the keys.
*/

// checks all buttons and plays/stops tones based on their state
void checkButtons() {
  for (int i = 0; i < NUM_KEYS; i++) {
    bool buttonPressed = (digitalRead(keys[i].buttonPin) == HIGH);

    if (buttonPressed && !keys[i].isPressed) {
      
      // apply debouncing to avoid false triggers
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

// stops playing the tone for a specific key
// if another key is still pressed, switches to playing that key's tone instead
// (this handles the case where you have multiple keys held down)
// PROBLEM: it falls back to the pressed key with the lowest index rather than the 
// one that was pressed last, could use a stack to solve this
void stopKeyTone(int keyIndex) {
  // check if any other key is still being pressed
  for (int i = 0; i < NUM_KEYS; i++) {
    if (keys[i].isPressed) {
      // found another pressed key, play its tone instead
      startKeyTone(i);
      return;
    }
  }

  // no other keys pressed, silence the speaker
  noTone(SPEAKER_PIN);
}

// light up key for 1 LED
void lightUpKey(int keyIndex, uint32_t color) {
  Adafruit_NeoPixel &seg = keys[keyIndex].led;
  seg.setPixelColor(0, color);
  seg.show();
}

// turns off 1 LED
void lightDownKey(int keyIndex) {
  Adafruit_NeoPixel &seg = keys[keyIndex].led;
  seg.setPixelColor(0, 0);
  seg.show();
}


// resets a key to its default state (LED off, servo at rest)
void resetKey(int keyIndex) {
  lightDownKey(keyIndex);
  autoReleaseKey(keyIndex);
}

void runBootTest() {
  // Flash all key LEDs white once, then off.
  for (int i = 0; i < NUM_KEYS; i++) {
    keys[i].led.setPixelColor(0, keys[i].led.Color(255, 255, 255));
    keys[i].led.show();
  }

  delay(300);

  for (int i = 0; i < NUM_KEYS; i++) {
    keys[i].led.setPixelColor(0, 0);
    keys[i].led.show();
  }
}