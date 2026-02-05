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
#include <stdarg.h>            // for va_list in custom logf function

// ============ DEBUG CONFIGURATION ============

// Set to 1 to enable debug output, 0 to disable
#define DEBUG_ENABLED 1
#define SERIAL_BAUD_RATE 115200

#if DEBUG_ENABLED

#define LOG_INIT()                                                             \
  do {                                                                         \
    Serial.begin(SERIAL_BAUD_RATE);                                            \
    while (!Serial) {                                                          \
      delay(10);                                                               \
    }                                                                          \
  } while (0)

#define LOG(x) Serial.print(x)
#define LOGLN(x) Serial.println(x)

// Lightweight printf that reads format string from PROGMEM (Flash)
// Saves RAM by storing strings in Flash instead of RAM
// Supports: %d, %u, %ld, %lu, %s, %c, %x, %X, %%
void LOG_F_P(const char *fmt_P, ...) {
  va_list args;
  va_start(args, fmt_P);

  char c;
  while ((c = pgm_read_byte(fmt_P++))) {
    if (c == '%') {
      c = pgm_read_byte(fmt_P++);

      // Check for 'l' modifier (long)
      bool isLong = false;
      if (c == 'l') {
        isLong = true;
        c = pgm_read_byte(fmt_P++);
      }

      switch (c) {
      case 'd': // Signed decimal
      case 'i':
        if (isLong)
          Serial.print(va_arg(args, long));
        else
          Serial.print(va_arg(args, int));
        break;

      case 'u': // Unsigned decimal
        if (isLong)
          Serial.print(va_arg(args, unsigned long));
        else
          Serial.print(va_arg(args, unsigned int));
        break;

      case 'x': // Hex lowercase
      case 'X': // Hex uppercase
        if (isLong)
          Serial.print(va_arg(args, unsigned long), HEX);
        else
          Serial.print(va_arg(args, unsigned int), HEX);
        break;

      case 's': // String (from RAM)
        Serial.print(va_arg(args, const char *));
        break;

      case 'c': // Character
        Serial.print((char)va_arg(args, int));
        break;

      case '%': // Literal %
        Serial.print('%');
        break;

      default: // Unknown - print as-is
        Serial.print('%');
        if (isLong)
          Serial.print('l');
        Serial.print(c);
        break;
      }
    } else {
      Serial.print(c);
    }
  }

  va_end(args);
}

// Macro to automatically wrap format strings with PSTR() for Flash storage
#define LOG_F(fmt, ...) LOG_F_P(PSTR(fmt), ##__VA_ARGS__)

#else

#define LOG_INIT()
#define LOG(x)
#define LOGLN(x)
#define LOG_F(...)

#endif

// ============ HELPER MACROS ============

#define LED(pin) Adafruit_NeoPixel(LEDS_PER_KEY, pin, NEO_GRB + NEO_KHZ800)
#define startKeyTone(keyIndex) tone(SPEAKER_PIN, keys[keyIndex].noteFreq)
#define servoPull(channel) servoDriver.setAngle(channel, SERVO_PRESS_ANGLE)
#define servoRest(channel) servoDriver.setAngle(channel, SERVO_REST_ANGLE)
#define autoPressKey(keyIndex) servoPull(keys[keyIndex].servoChannel)
#define autoReleaseKey(keyIndex) servoRest(keys[keyIndex].servoChannel)

// ============ HARDWARE & SEQUENCE DEFINITIONS ============

Key keys[NUM_KEYS] = {
    {KEY0_BUTTON_PIN, LED(KEY0_LED_PIN), KEY0_SERVO_CHANNEL, KEY0_NOTE, false}, // C4
    {KEY1_BUTTON_PIN, LED(KEY1_LED_PIN), KEY1_SERVO_CHANNEL, KEY1_NOTE, false}  // D4
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
  LOGLN(F("\n========================================"));
  LOGLN(F("    OPEN OCTAVE FIRMWARE V3 - INIT"));
  LOGLN(F("========================================"));

  LOG(F("[SETUP] Configuring speaker... "));
  pinMode(SPEAKER_PIN, OUTPUT);
  noTone(SPEAKER_PIN);
  LOG_F("OK (speaker_pin: %d)\n", SPEAKER_PIN);

  LOG(F("[SETUP] Initializing I2C... "));
  Wire.begin();
  LOGLN(F("OK"));

  LOG(F("[SETUP] Initializing servo driver... "));
  servoDriver.init();
  servoDriver.setFrequency(SERVO_FREQ);
  LOG_F("OK (freq: %dHz)\n", SERVO_FREQ);

  // initialize each key
  LOGLN(F("[SETUP] Initializing keys:"));
  for (int i = 0; i < NUM_KEYS; i++) {
    pinMode(keys[i].buttonPin, INPUT);
    servoRest(keys[i].servoChannel);

    keys[i].led.begin();
    keys[i].led.setBrightness(LED_BRIGHTNESS);
    keys[i].led.show();
    keys[i].isPressed = false;

    LOG_F("  Key %d: btn_pin=%d, led_pin=%d, servo_ch=%d, freq=%dHz\n", 
      i, keys[i].buttonPin, keys[i].led.getPin(), keys[i].servoChannel, keys[i].noteFreq);
  }
  LOG_F("OK (%d keys initialized)\n", NUM_KEYS);

  LOG(F("[SETUP] Running startup LED test..."));
  runLEDTest();
  LOGLN(F("OK"));

  LOGLN(F("========================================"));
  LOG_F("[SETUP] Complete! Starting in %s mode\n", getCurrentModeString());
  LOGLN(F("========================================\n"));
}

// runs repeatedly forever
void loop() {
  processSerialCommands(); // check for serial commands
  checkButtons();          // detect any key presses and play sounds

  // if we're in an automatic mode, handle the sequence playback
  if (currentMode != MANUAL) {
    handleAutomaticModes();
  }
}

/*
===============================
    MODE CONTROL FUNCTIONS
===============================
These handle switching between and handling the MANUAL, AUTOMATIC_LEDS, and FULL_AUTOMATIC modes.
*/

void processSerialCommands() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();

    // Convert to lowercase for case-insensitive commands
    if (cmd >= 'A' && cmd <= 'Z') {
      cmd = cmd + ('a' - 'A');
    }

    switch (cmd) {
    case 'm': // Manual mode
      LOGLN(F("\n[CMD] Received: Switch to MANUAL mode"));
      setMode(MANUAL);
      break;

    case 'a': // Automatic LEDs mode
      LOGLN(F("\n[CMD] Received: Switch to AUTOMATIC_LEDS mode"));
      setMode(AUTOMATIC_LEDS);
      startSequence();
      break;

    case 'f': // Full automatic mode
      LOGLN(F("\n[CMD] Received: Switch to FULL_AUTOMATIC mode"));
      setMode(FULL_AUTOMATIC);
      startSequence();
      break;

    case 's': // Start sequence (in current mode)
      if (currentMode != MANUAL) {
        LOGLN(F("\n[CMD] Received: Start sequence"));
        startSequence();
      } else {
        LOGLN(F("\n[CMD] Cannot start sequence in MANUAL mode"));
      }
      break;

    case 'x': // Stop sequence
      LOGLN(F("\n[CMD] Received: Stop sequence"));
      stopSequence();
      break;

    case 'h': // Help
    case '?':
      LOGLN(F("\n========================================"));
      LOGLN(F("         SERIAL COMMANDS"));
      LOGLN(F("========================================"));
      LOGLN(F("  m - Switch to MANUAL mode"));
      LOGLN(F("  a - Switch to AUTOMATIC_LEDS mode"));
      LOGLN(F("  f - Switch to FULL_AUTOMATIC mode"));
      LOGLN(F("  s - Start sequence"));
      LOGLN(F("  x - Stop sequence"));
      LOGLN(F("  h - Show this help"));
      LOGLN(F("========================================\n"));
      break;

    case '\n':
    case '\r':
    case ' ':
      // Ignore whitespace
      break;

    default:
      LOG_F("[CMD] Unknown command: '%c' (type 'h' for help)\n", cmd);
      break;
    }
  }
}

// switches to a new mode and resets everything to a clean state
void setMode(Mode mode) {
  LOGLN(F("[MODE] Resetting all keys..."));

  for (int i = 0; i < NUM_KEYS; i++) {
    resetKey(i);
  }

  sequenceRunning = false;
  currentMode = mode;
  LOG_F("[MODE] Switched to mode %s\n", getCurrentModeString());
}

// handles automatic sequence playback
void handleAutomaticModes() {
  if (!sequenceRunning)
    return;

  unsigned long elapsed = millis() - currentStepStartTime;
  if (elapsed >= sequence[currentSequenceStep].duration) {
    LOG_F("[SEQ] Step %d complete (elapsed: %lums)\n", currentSequenceStep,
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
  LOGLN(F("\n[SEQ] ======== STARTING SEQUENCE ========"));
  LOG_F("[SEQ] Total steps: %d\n", SEQUENCE_LENGTH);

  sequenceRunning = true;
  currentSequenceStep = 0;
  currentStepStartTime = millis();

  // immediately play the first step
  executeSequenceStep(sequence[currentSequenceStep]);
}

// stops the sequence and turns off all keys
void stopSequence() {
  LOGLN(F("[SEQ] ======== SEQUENCE COMPLETE ========"));
  LOGLN(F("[SEQ] Resetting all keys..."));

  for (int i = 0; i < NUM_KEYS; i++) {
    resetKey(i);
  }

  sequenceRunning = false;
  LOGLN(F("[SEQ] Sequence stopped\n"));
}

// plays a single step of a sequence
void executeSequenceStep(const SequenceStep &step) {
  LOG_F("[SEQ] Step %d/%d: key=%d, color=%s, duration=%dms\n",
       currentSequenceStep + 1, SEQUENCE_LENGTH, step.keyIndex,
       getColorString(step.color), step.duration);

  // light up the key's LED with the specified color
  lightUpKey(step.keyIndex, step.color);

  // if we're in full automatic mode, also press the key with the servo
  if (currentMode == FULL_AUTOMATIC) {
    LOG_F("[SERVO] Auto-pressing key %d (channel %d)\n", step.keyIndex,
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
        LOG_F("[KEY] Key %d PRESSED (pin %d, freq %dHz)\n", i, keys[i].buttonPin,
             keys[i].noteFreq);
        startKeyTone(i);
      }

    } else if (!buttonPressed && keys[i].isPressed) {
      keys[i].isPressed = false;
      LOG_F("[KEY] Key %d RELEASED\n", i);
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
  LOG_F("[LED] Key %d LED ON: color=%s\n", keyIndex, getColorString(color));

  for (int i = 0; i < LEDS_PER_KEY; i++) {
    keys[keyIndex].led.setPixelColor(i, color);
  }

  keys[keyIndex].led.show();
}

// turns off all LEDs on a key's LED strip
void lightDownKey(int keyIndex) {
  LOG_F("[LED] Key %d OFF\n", keyIndex);

  for (int i = 0; i < LEDS_PER_KEY; i++) {
    keys[keyIndex].led.setPixelColor(i, 0);
  }

  keys[keyIndex].led.show();
}

// resets a key to its default state (LED off, servo at rest)
void resetKey(int keyIndex) {
  lightDownKey(keyIndex);
  autoReleaseKey(keyIndex);
}

void runLEDTest() {
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