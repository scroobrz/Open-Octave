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

#include "PCA9685.h" // controls the PCA9685 servo motor driver (I2C)
#include "firmware_v3_config.h"
#include <Adafruit_NeoPixel.h> // controls the LED sticks/strips
#include <Wire.h>              // allows I2C communication with the servo driver

// ============ DEBUG CONFIGURATION ============

// Set to 1 to enable debug output, 0 to disable
#define DEBUG_ENABLED 1
#define DEBUG_BAUD_RATE 115200

#if DEBUG_ENABLED
#define LOG_INIT()                                                             \
  do {                                                                         \
    Serial.begin(DEBUG_BAUD_RATE);                                             \
    while (!Serial) {                                                          \
      delay(10);                                                               \
    }                                                                          \
  } while (0)
#define LOG(x) Serial.print(x)
#define LOGLN(x) Serial.println(x)
#define LOGF(str, ...)                                                         \
  do {                                                                         \
    char _buf[128];                                                            \
    snprintf(_buf, sizeof(_buf), str, ##__VA_ARGS__);                          \
    Serial.print(_buf);                                                        \
  } while (0)
#else
#define LOG_INIT()
#define LOG(x)
#define LOGLN(x)
#define LOGF(...)
#endif

// ============ HELPER MACROS ============

#define startKeyTone(keyIndex) tone(SPEAKER_PIN, keys[keyIndex].noteFreq)
#define servoPull(channel) servoDriver.setAngle(channel, SERVO_PRESS_ANGLE)
#define servoRest(channel) servoDriver.setAngle(channel, SERVO_REST_ANGLE)
#define autoPressKey(keyIndex) servoPull(keys[keyIndex].servoChannel)
#define autoReleaseKey(keyIndex) servoRest(keys[keyIndex].servoChannel)

// ============ HARDWARE & SEQUENCE DEFINITIONS ============

Key keys[NUM_KEYS] = {
    {KEY0_BUTTON_PIN, KEY0_SERVO_CHANNEL, KEY0_NOTE, false}, // C4
    {KEY1_BUTTON_PIN, KEY1_SERVO_CHANNEL, KEY1_NOTE, false}  // D4
};

const SequenceStep sequence[SEQUENCE_LENGTH] = {
    {0, COLOR_BLUE, 500},
    {1, COLOR_GREEN, 500},
    {0, COLOR_BLUE, 500},
    {1, COLOR_RED, 500}
};

ServoDriver servoDriver; // controls all servos via I2C
Adafruit_NeoPixel strip(NUM_LEDS, STRIP_DATA_PIN, NEO_GRB + NEO_KHZ800);

// ============ GLOBAL STATE ============

Mode currentMode = MANUAL;
unsigned long lastModeSwitchTime = 0; // when mode switch was last pressed (for debouncing)
bool previousModeSwitchState = LOW; // previous state of mode switch button for edge detection
unsigned long lastKeyPressTime[NUM_KEYS] = {0}; // when each key was last pressed (for debouncing)
bool sequenceRunning = false;
int currentSequenceStep = 0;
unsigned long currentStepStartTime = 0;

// ============ HELPERS FOR LOGGING ============

const char *getCurrentModeString() {
    switch (currentMode) {
    case MANUAL:
        return "MANUAL";
    case AUTOMATIC_LEDS:
        return "AUTOMATIC_LEDS";
    case FULL_AUTOMATIC:
        return "FULL_AUTOMATIC";
    default:
        return "UNKNOWN";
    }
}

const char *getColorString(uint32_t color) {
    switch (color) {
    case COLOR_RED:
        return "RED";
    case COLOR_GREEN:
        return "GREEN";
    case COLOR_BLUE:
        return "BLUE";
    case COLOR_WHITE:
        return "WHITE";
    default:
        return "CUSTOM";
    }
}

/*
===============================
     CORE ARDUINO FUNCTIONS
===============================
These are the two functions that Arduino calls automatically.
*/

// runs once
void setup() {
  LOG_INIT();
  LOGLN("\n========================================");
  LOGLN("    OPEN OCTAVE FIRMWARE V3 - INIT");
  LOGLN("========================================");

  LOG("[SETUP] Configuring pins... ");
  pinMode(MODE_SWITCH_PIN, INPUT);
  pinMode(SPEAKER_PIN, OUTPUT);
  LOGF("OK (mode_switch_pin: %d, speaker_pin: %d)\n", MODE_SWITCH_PIN, SPEAKER_PIN);

  LOG("[SETUP] Initializing I2C... ");
  Wire.begin();
  LOGLN("OK");

  LOG("[SETUP] Initializing servo driver... ");
  servoDriver.init();
  servoDriver.setFrequency(SERVO_FREQ);
  LOGF("OK (freq: %dHz)\n", SERVO_FREQ);

  LOG("[SETUP] Initializing LED strip... ");
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.show();
  LOGF("OK (%d LEDs on pin %d at brightness %d)\n", NUM_LEDS, STRIP_DATA_PIN, strip.getBrightness());

  // initialize each key
  LOGLN("[SETUP] Initializing keys:");
  for (int i = 0; i < NUM_KEYS; i++) {
    pinMode(keys[i].buttonPin, INPUT);
    servoRest(keys[i].servoChannel);
    keys[i].isPressed = false;
    LOGF("  Key %d: btn_pin=%d, servo_ch=%d, freq=%dHz\n", i, keys[i].buttonPin,
         keys[i].servoChannel, keys[i].noteFreq);
  }
  LOGF("OK (%d keys initialized)\n", NUM_KEYS);

  LOGLN("========================================");
  LOGF("[SETUP] Complete! Starting in %s mode\n", getCurrentModeString());
  LOGLN("========================================\n");
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
These handle switching between and handling the MANUAL, AUTOMATIC_LEDS, and
FULL_AUTOMATIC modes.
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

    LOGLN("\n[MODE] Mode switch button pressed!");
    LOGF("[MODE] Current mode: %s\n", getCurrentModeString());

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
  LOGLN("[MODE] Resetting all keys...");

  for (int i = 0; i < NUM_KEYS; i++) {
    resetKey(i);
  }

  sequenceRunning = false;
  currentMode = mode;
  LOGF("[MODE] Switched to mode %s\n", getCurrentModeString());
}

// handles automatic sequence playback
void handleAutomaticModes() {
  if (!sequenceRunning)
    return;

  unsigned long elapsed = millis() - currentStepStartTime;
  if (elapsed >= sequence[currentSequenceStep].duration) {
    LOGF("[SEQ] Step %d complete (elapsed: %lums)\n", currentSequenceStep,
         elapsed);
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
  LOGLN("\n[SEQ] ======== STARTING SEQUENCE ========");
  LOGF("[SEQ] Total steps: %d\n", SEQUENCE_LENGTH);

  sequenceRunning = true;
  currentSequenceStep = 0;
  currentStepStartTime = millis();

  // immediately play the first step
  executeSequenceStep(sequence[currentSequenceStep]);
}

// stops the sequence and turns off all keys
void stopSequence() {
  LOGLN("[SEQ] ======== SEQUENCE COMPLETE ========");
  LOGLN("[SEQ] Resetting all keys...");

  for (int i = 0; i < NUM_KEYS; i++) {
    resetKey(i);
  }

  sequenceRunning = false;
  LOGLN("[SEQ] Sequence stopped\n");
}

// plays a single step of a sequence
void executeSequenceStep(const SequenceStep &step) {
  LOGF("[SEQ] Step %d/%d: key=%d, color=%s, duration=%dms\n",
       currentSequenceStep + 1, SEQUENCE_LENGTH, step.keyIndex,
       getColorString(step.color), step.duration);

  // light up the key's LED with the specified color
  lightUpKey(step.keyIndex, step.color);

  // if we're in full automatic mode, also press the key with the servo
  if (currentMode == FULL_AUTOMATIC) {
    LOGF("[SERVO] Auto-pressing key %d (channel %d)\n", step.keyIndex,
         keys[step.keyIndex].servoChannel);
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
    bool buttonPressed = digitalRead(keys[i].buttonPin) == HIGH;

    if (buttonPressed && !keys[i].isPressed) {

      // apply debouncing to avoid false triggers
      if (millis() - lastKeyPressTime[i] >= DEBOUNCE_DELAY) {
        keys[i].isPressed = true;
        lastKeyPressTime[i] = millis();
        LOGF("[KEY] Key %d PRESSED (pin %d, freq %dHz)\n", i, keys[i].buttonPin,
             keys[i].noteFreq);
        startKeyTone(i);
      }

    } else if (!buttonPressed && keys[i].isPressed) {
      keys[i].isPressed = false;
      LOGF("[KEY] Key %d RELEASED\n", i);
      stopKeyTone(i);
    }
  }
}

// stops playing the tone for a specific key
// if another key is still pressed, switches to playing that key's tone instead
// (this handles the case where you have multiple keys held down)
// PROBLEM: it falls back to the pressed key with the lowest index rather than
// the one that was pressed last, could use a stack to solve this
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

// lights up all LEDs on a key's LED strip with the specified color
void lightUpKey(int keyIndex, uint32_t color) {
  int start = keyIndex * LEDS_PER_KEY;
  int end = start + LEDS_PER_KEY;

  LOGF("[LED] Key %d LED ON: color=%s, LEDs %d-%d\n", keyIndex,
       getColorString(color), start, end - 1);

  for (int i = start; i < end; i++) {
    strip.setPixelColor(i, color);
  }

  strip.show();
}

// turns off all LEDs on a key's LED strip
void lightDownKey(int keyIndex) {
  int start = keyIndex * LEDS_PER_KEY;
  int end = start + LEDS_PER_KEY;

  LOGF("[LED] Key %d OFF: LEDs %d-%d\n", keyIndex, start, end - 1);

  for (int i = start; i < end; i++) {
    strip.setPixelColor(i, 0);
  }

  strip.show();
}

// resets a key to its default state (LED off, servo at rest)
void resetKey(int keyIndex) {
  lightDownKey(keyIndex);
  autoReleaseKey(keyIndex);
}