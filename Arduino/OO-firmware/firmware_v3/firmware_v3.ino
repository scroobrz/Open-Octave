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

#include "PCA9685.h"               // controls the PCA9685 servo motor driver (I2C)
#include "firmware_v3_config.h"    // global configuration for the firmware
#include "firmware_v3_debug.h"     // debug logging macros (LOG, LOGLN, LOGF)
#include "firmware_v3_sequences.h" // sequence definitions
#include <Adafruit_NeoPixel.h>     // controls the LED sticks/strips
#include <Wire.h>                  // allows I2C communication with the servo driver

// ============ HARDWARE DEFINITIONS ============

Key keys[NUM_KEYS] = {
    {KEY0_BUTTON_PIN, KEY0_LED_PIN, nullptr, KEY0_SERVO_CHANNEL, KEY0_NOTE, false}, // C4
    {KEY1_BUTTON_PIN, KEY1_LED_PIN, nullptr, KEY1_SERVO_CHANNEL, KEY1_NOTE, false}, // D4
    {KEY2_BUTTON_PIN, KEY2_LED_PIN, nullptr, KEY2_SERVO_CHANNEL, KEY2_NOTE, false}  // E4
};

ServoDriver servoDriver; // controls all servos via I2C

// ============ GLOBAL STATE ============

Mode currentMode = MANUAL;
unsigned long lastModeSwitchTime = 0;
bool previousModeSwitchState = LOW;
unsigned long lastKeyPressTime[NUM_KEYS] = {0};
unsigned long toneStartTime[NUM_KEYS] = {0};  // Tracks when each key's tone started
bool sequenceRunning = false;
int currentSequenceStepIndex = 0;
unsigned long currentStepStartTime = 0;
int currentSequenceIndex = 0;

// For non-blocking delay between consecutive same-key steps
bool waitingForServoRelease = false;
unsigned long servoReleaseStartTime = 0;

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

// ============ TEST LOGGING (CSV STREAM) ============
// Prints one CSV row per event to Serial (no RAM buffering).

enum TestLogErrorCode : uint8_t {
  TESTLOG_OK = 0,
  TESTLOG_INVALID_STEP_INDEX = 1,
  TESTLOG_INVALID_KEY_INDEX = 2
};

bool testLogEnabled = false;
uint16_t testLogRunId = 0;
uint16_t testLogEventId = 0;

// Manual repeat detection
int8_t testLogLastManualKey = -1;
unsigned long testLogLastManualTime = 0;
uint8_t testLogManualRepeatStreak = 0;

// Auto repeat detection + timing
unsigned long testLogExpectedNextStepStartTime = 0;
int8_t testLogLastAutoKey = -1;
uint8_t testLogAutoRepeatStreak = 0;

static void testLogPrintHeader() {
  Serial.println();
  Serial.println(F("CSV_BEGIN"));
  Serial.println(F("run_id,event_id,mode,event_type,key_index,repeat_streak,input_to_audio_ms,step_led_cmd_ms,step_servo_cmd_ms,autoplay_timing_error_ms,success,error_code"));
  Serial.println(F("CSV_DATA"));
}

static void testLogStart() {
  testLogEnabled = true;
  testLogRunId++;
  testLogEventId = 0;

  testLogLastManualKey = -1;
  testLogLastManualTime = 0;
  testLogManualRepeatStreak = 0;

  testLogExpectedNextStepStartTime = 0;
  testLogLastAutoKey = -1;
  testLogAutoRepeatStreak = 0;

  LOGLN("\n[TESTLOG] Enabled. Streaming CSV to Serial. Press 'g' again to stop.");
  testLogPrintHeader();
}

static void testLogStop() {
  if (!testLogEnabled) return;
  testLogEnabled = false;
  Serial.println(F("CSV_END"));
  LOGF("[TESTLOG] Disabled (run_id=%u, events=%u)\n", testLogRunId, testLogEventId);
}

static void testLogLogManualPress(int keyIndex, unsigned long pressDetectedMs, unsigned long audioStartedMs) {
  if (!testLogEnabled) return;

  if (keyIndex == testLogLastManualKey && (pressDetectedMs - testLogLastManualTime) <= 1000) {
    testLogManualRepeatStreak++;
  } else {
    testLogManualRepeatStreak = 1;
  }
  testLogLastManualKey = (int8_t)keyIndex;
  testLogLastManualTime = pressDetectedMs;

  testLogEventId++;

  long latency = (long)(audioStartedMs - pressDetectedMs);

  Serial.print(testLogRunId); Serial.print(",");
  Serial.print(testLogEventId); Serial.print(",");
  Serial.print(getCurrentModeString()); Serial.print(",");
  Serial.print(F("MANUAL_PRESS")); Serial.print(",");
  Serial.print(keyIndex); Serial.print(",");
  Serial.print(testLogManualRepeatStreak); Serial.print(",");
  Serial.print(latency); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(1); Serial.print(",");
  Serial.println(TESTLOG_OK);
}

static void testLogLogAutoStep(int keyIndex, long timingErrorMs, unsigned long ledCmdMs, unsigned long servoCmdMs, uint16_t stepDurationMs, bool nextIsSameKey) {
  if (!testLogEnabled) return;

  if (keyIndex == testLogLastAutoKey) testLogAutoRepeatStreak++;
  else testLogAutoRepeatStreak = 1;
  testLogLastAutoKey = (int8_t)keyIndex;

  testLogEventId++;

  Serial.print(testLogRunId); Serial.print(",");
  Serial.print(testLogEventId); Serial.print(",");
  Serial.print(getCurrentModeString()); Serial.print(",");
  Serial.print(F("AUTO_STEP")); Serial.print(",");
  Serial.print(keyIndex); Serial.print(",");
  Serial.print(testLogAutoRepeatStreak); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print((long)ledCmdMs); Serial.print(",");
  Serial.print((long)servoCmdMs); Serial.print(",");
  Serial.print((long)timingErrorMs); Serial.print(",");
  Serial.print(1); Serial.print(",");
  Serial.println(TESTLOG_OK);

  // Update expected time for next step (includes same-key release delay)
  testLogExpectedNextStepStartTime = testLogExpectedNextStepStartTime + (unsigned long)stepDurationMs;
  if (nextIsSameKey) {
    testLogExpectedNextStepStartTime = testLogExpectedNextStepStartTime + SERVO_RELEASE_DELAY;
  }
}

static void testLogLogError(uint8_t errorCode, const __FlashStringHelper* eventType) {
  if (!testLogEnabled) return;

  testLogEventId++;

  Serial.print(testLogRunId); Serial.print(",");
  Serial.print(testLogEventId); Serial.print(",");
  Serial.print(getCurrentModeString()); Serial.print(",");
  Serial.print(eventType); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(0); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(0); Serial.print(",");
  Serial.println(errorCode);
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

  // ===== VALIDATION =====
  LOG("[SETUP] Validating hardware config... ");
  if (!validateHardwareInit()) {
    LOGLN("[ERROR] CRITICAL: Hardware validation failed!");
    LOGLN("System halted. Check configuration.");
    while (true) {
      delay(1000);
    } // Halt - configuration error
  }
  LOGLN("OK");

  LOG("[SETUP] Validating sequence data... ");
  if (!validateSequenceData()) {
    LOGLN("WARNING: Sequence data has errors! Automatic modes may not work correctly.");
    // Continue but automatic modes may not work correctly
  } else {
    LOGLN("OK");
  }

  // ===== INITIALIZATION =====
  LOG("[SETUP] Configuring speaker... ");
  pinMode(SPEAKER_PIN, OUTPUT);
  noTone(SPEAKER_PIN);
  LOGF("OK (speaker_pin: %d)\n", SPEAKER_PIN);

  LOG("[SETUP] Initializing I2C... ");
  Wire.begin();
  LOGLN("OK");

  LOG("[SETUP] Initializing servo driver... ");
  servoDriver.init();
  servoDriver.setFrequency(SERVO_FREQ);
  LOGF("OK (freq: %dHz)\n", SERVO_FREQ);

  // initialize each key
  LOGLN("[SETUP] Initializing keys:");
  for (int i = 0; i < NUM_KEYS; i++) {
    pinMode(keys[i].buttonPin, INPUT);
    servoRest(keys[i].servoChannel);

    // Create NeoPixel object dynamically (can't be done at global scope)
    keys[i].led = new Adafruit_NeoPixel(LEDS_PER_KEY, keys[i].ledPin, NEO_GRB + NEO_KHZ800);
    keys[i].led->begin();
    keys[i].led->setBrightness(LED_BRIGHTNESS);
    keys[i].led->show();
    keys[i].isPressed = false;

    LOGF("  Key %d: btn_pin=%d, led_pin=%d, servo_ch=%d, freq=%dHz\n", i,
         keys[i].buttonPin, keys[i].ledPin, keys[i].servoChannel,
         keys[i].noteFreq);
  }
  LOGF("OK (%d keys initialized)\n", NUM_KEYS);

  LOGLN("========================================");
  LOGF("[SETUP] Complete! Starting in %s mode\n", getCurrentModeString());
  LOGLN("========================================\n");
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
These handle switching between and handling the MANUAL, AUTOMATIC_LEDS, and
FULL_AUTOMATIC modes.
*/

void processSerialCommands() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();

    // Convert to lowercase
    if (cmd >= 'A' && cmd <= 'Z') {
      cmd = cmd + ('a' - 'A');
    }

    switch (cmd) {

    // ---- MODE CONTROL ----

    case 'm': // Manual mode
      LOGLN("\n[CMD] Received: Switch to MANUAL mode");
      if (currentMode == MANUAL) {
        LOGLN("\n[CMD] Already in MANUAL mode");
      } else {
        setMode(MANUAL);
      }
      break;

    case 'a': // Automatic LEDs mode
      LOGLN("\n[CMD] Received: Switch to AUTOMATIC_LEDS mode");
      if (currentMode == AUTOMATIC_LEDS) {
        LOGLN("\n[CMD] Already in AUTOMATIC_LEDS mode");
      } else {
        setMode(AUTOMATIC_LEDS);
      }
      break;

    case 'f': // Full automatic mode
      LOGLN("\n[CMD] Received: Switch to FULL_AUTOMATIC mode");
      if (currentMode == FULL_AUTOMATIC) {
        LOGLN("\n[CMD] Already in FULL_AUTOMATIC mode");
      } else {
        setMode(FULL_AUTOMATIC);
      }
      break;

    // ---- SEQUENCE CONTROL ----

    case 's': // Start sequence
      LOGLN("\n[CMD] Received: Start sequence");
      if (currentMode == MANUAL) {
        LOGLN("\n[CMD] Cannot start sequence in MANUAL mode");
      } else {
        startSequence();
      }
      break;

    case 'x': // Stop sequence
      LOGLN("\n[CMD] Received: Stop sequence");
      if (currentMode == MANUAL) {
        LOGLN("\n[CMD] Cannot stop sequence in MANUAL mode");
      } else if (!sequenceRunning) {
        LOGLN("\n[CMD] Sequence is not running");
      } else {
        stopSequence();
      }
      break;

    case 'n': // Next sequence
      LOGLN("\n[CMD] Received: Next sequence");
      nextSequence();
      break;

    case 'p': // Previous sequence
      LOGLN("\n[CMD] Received: Previous sequence");
      prevSequence();
      break;

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      // Direct sequence selection by number
      {
        int seqIndex = cmd - '0';
        LOGF("\n[CMD] Received: Select sequence %d\n", seqIndex);
        selectSequence(seqIndex);
      }
      break;

    case 'l': // List sequences
      LOGLN("\n========================================");
      LOGLN("         AVAILABLE SEQUENCES");
      LOGLN("========================================");
      for (int i = 0; i < NUM_SEQUENCES; i++) {
        LOGF("  %d - %s (%d steps)%s\n", 
             i, sequences[i].name, sequences[i].length,
             (i == currentSequenceIndex) ? " [ACTIVE]" : "");
      }
      LOGLN("========================================\n");
      break;

    // ---- TESTING ----

    case 't': // Test LEDs
      LOGLN("\n[CMD] Received: Test LEDs");
      testLEDs();
      break;

    case 'u': // Test servos
      LOGLN("\n[CMD] Received: Test servos");
      testServos();
      break;

    case 'g': // Toggle test log mode
      if (!testLogEnabled) {
        LOGLN("\n[CMD] Received: Enable test log mode");
        testLogStart();
      } else {
        LOGLN("\n[CMD] Received: Disable test log mode");
        testLogStop();
      }
      break;


    // ---- HELP ----

    case 'h': // Help
    case '?':
      LOGLN("\n========================================");
      LOGLN("         SERIAL COMMANDS");
      LOGLN("========================================");
      LOGLN("  MODE:");
      LOGLN("    m - Switch to MANUAL mode");
      LOGLN("    a - Switch to AUTOMATIC_LEDS mode");
      LOGLN("    f - Switch to FULL_AUTOMATIC mode");
      LOGLN("  SEQUENCE:");
      LOGLN("    s - Start sequence");
      LOGLN("    x - Stop sequence");
      LOGLN("    n - Next sequence");
      LOGLN("    p - Previous sequence");
      LOGLN("    0-9 - Select sequence by number");
      LOGLN("    l - List all sequences");
      LOGLN("  TESTING:");
      LOGLN("    t - Test LEDs");
      LOGLN("    u - Test servos");
      LOGLN("    g - Enter/Exit test log mode");
      LOGLN("  HELP:");
      LOGLN("    h - Show this help");
      LOGLN("========================================\n");
      break;

    case '\n':
    case '\r':
    case ' ':
      // Ignore
      break;

    default:
      LOGF("[CMD] Unknown command: '%c' (type 'h' for help)\n", cmd);
      break;
    }
  }
}

// switches to a new mode and resets everything to a clean state
void setMode(Mode mode) {
  stopSequence();
  currentMode = mode;
  LOGF("[MODE] Switched to mode %s\n", getCurrentModeString());
}

// handles automatic sequence playback
void handleAutomaticModes() {
  if (!sequenceRunning)
    return;

  // Defensive check: ensure currentSequenceStep is valid
  if (currentSequenceStepIndex < 0 || currentSequenceStepIndex >= currentSequence.length) {
    LOGF("[ERROR] Invalid step index: %d encountered while handling automatic modes\n", currentSequenceStepIndex);
    testLogLogError(TESTLOG_INVALID_STEP_INDEX, F("ERROR_INVALID_STEP_INDEX"));
    stopSequence();
    return;
  }

  // If we're waiting for the servo to release (between consecutive same-key steps)
  if (waitingForServoRelease) {
    if (millis() - servoReleaseStartTime >= SERVO_RELEASE_DELAY) {
      // Delay complete, now execute the next step
      waitingForServoRelease = false;
      executeSequenceStep(currentSequenceStep);
    }
    // While waiting, checkButtons() still runs in the main loop
    return;
  }

  if (millis() - currentStepStartTime >= currentSequenceStep.duration) {
    LOGF("[SEQ] Step %d complete\n", currentSequenceStepIndex);
    
    // Remember which key we're resetting before incrementing step index
    uint8_t previousKeyIndex = currentSequenceStep.keyIndex;
    resetKey(previousKeyIndex);

    currentSequenceStepIndex++;
    if (currentSequenceStepIndex >= currentSequence.length) {
      LOGLN("[SEQ] Sequence complete");
      stopSequence();
      return;
    }

    // If next step uses the same key, wait for servo to physically release
    if (currentSequenceStep.keyIndex == previousKeyIndex) {
      LOGF("[SEQ] Same key %d in consecutive steps - waiting for servo release\n", previousKeyIndex);
      waitingForServoRelease = true;
      servoReleaseStartTime = millis();
      // Don't execute step yet - will be done on next loop iteration after delay
    } else {
      executeSequenceStep(currentSequenceStep);
    }
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
  if (sequenceRunning) {
    LOGLN("[SEQ] Sequence already running, ignoring start request");
    return;
  }
  
  if (currentSequence.length <= 0) {
    LOGF("[ERROR] Invalid sequence length: %d encountered while starting sequence\n", currentSequence.length);
    return;
  }

  LOGLN("\n[SEQ] ======== STARTING SEQUENCE ========");
  LOGF("[SEQ] Sequence: %s (%d steps)\n", currentSequence.name, currentSequence.length);

  sequenceRunning = true;
  currentSequenceStepIndex = 0;
  currentStepStartTime = millis();

  if (testLogEnabled) {
    testLogExpectedNextStepStartTime = currentStepStartTime;
    testLogLastAutoKey = -1;
    testLogAutoRepeatStreak = 0;
  }

  // immediately play the first step
  executeSequenceStep(currentSequenceStep);
}

// stops the sequence and turns off all keys
void stopSequence() {
  LOGLN("\n[SEQ] ======== STOPPING SEQUENCE ========");
  LOGF("[SEQ] Total steps completed: %d\n", currentSequenceStepIndex);

  for (int i = 0; i < NUM_KEYS; i++) {
    resetKey(i);
  }
  
  // Ensure speaker is silenced (resetKey bypasses normal release detection)
  noTone(SPEAKER_PIN);

  if (testLogEnabled) {
    testLogExpectedNextStepStartTime = 0;
    testLogLastAutoKey = -1;
    testLogAutoRepeatStreak = 0;
  }

  
  sequenceRunning = false;
  waitingForServoRelease = false;
}

// plays a single step of a sequence
void executeSequenceStep(const SequenceStep &step) {
  if (!IS_VALID_KEY_INDEX(step.keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while executing sequence step\n", step.keyIndex);
    testLogLogError(TESTLOG_INVALID_KEY_INDEX, F("ERROR_INVALID_KEY"));
    currentStepStartTime = millis(); // Still update time to prevent infinite loop
    return;
  }

  LOGF("[SEQ] Step %d/%d: key=%d, color=%s, duration=%dms\n",
       currentSequenceStepIndex + 1, currentSequence.length, 
       step.keyIndex, getColorString(step.color), step.duration);

    unsigned long stepStartCallTime = millis();

  // Compute autoplay timing error against expected time
  long autoplayTimingErrorMs = 0;
  if (testLogEnabled) {
    if (testLogExpectedNextStepStartTime == 0) {
      testLogExpectedNextStepStartTime = stepStartCallTime;
    }
    autoplayTimingErrorMs = (long)(stepStartCallTime - testLogExpectedNextStepStartTime);
  }

  unsigned long ledCmdStart = millis();
  // light up the key's LED with the specified color
  lightUpKey(step.keyIndex, step.color);
  unsigned long ledCmdLatencyMs = millis() - ledCmdStart;

  unsigned long servoCmdLatencyMs = 0;
  // if we're in full automatic mode, also press the key with the servo
  if (currentMode == FULL_AUTOMATIC) {
    LOGF("[SERVO] Auto-pressing key %d (channel %d)\n", step.keyIndex, keys[step.keyIndex].servoChannel);
    unsigned long servoCmdStart = millis();
    autoPressKey(step.keyIndex);
    servoCmdLatencyMs = millis() - servoCmdStart;
  }

  if (testLogEnabled) {
    int nextIndex = currentSequenceStepIndex + 1;
    bool nextIsSameKey = false;
    if (nextIndex >= 0 && nextIndex < currentSequence.length) {
      nextIsSameKey = (currentSequence.steps[nextIndex].keyIndex == step.keyIndex);
    }
    testLogLogAutoStep(step.keyIndex, autoplayTimingErrorMs, ledCmdLatencyMs, servoCmdLatencyMs, (uint16_t)step.duration, nextIsSameKey);
  }

  currentStepStartTime = millis();
}

// Select a specific sequence by index
void selectSequence(int index) {
  if (index < 0 || index >= NUM_SEQUENCES) {
    LOGF("[ERROR] Invalid sequence index: %d (valid: 0-%d)\n", index, NUM_SEQUENCES - 1);
    return;
  }
  
  // Stop current sequence if running
  if (sequenceRunning) {
    stopSequence();
  }
  
  currentSequenceIndex = index;
  LOGF("[SEQ] Selected sequence %d: %s (%d steps)\n", 
       index, sequences[index].name, sequences[index].length);
}

// Cycle to next sequence
void nextSequence() {
  int newIndex = (currentSequenceIndex + 1) % NUM_SEQUENCES;
  selectSequence(newIndex);
}

// Cycle to previous sequence
void prevSequence() {
  int newIndex = (currentSequenceIndex - 1 + NUM_SEQUENCES) % NUM_SEQUENCES;
  selectSequence(newIndex);
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
        unsigned long pressDetectedMs = millis();

        keys[i].isPressed = true;
        lastKeyPressTime[i] = pressDetectedMs;
        toneStartTime[i] = pressDetectedMs;  // Track when this tone started

        LOGF("[KEY] Key %d PRESSED (pin %d, freq %dHz)\n", i, keys[i].buttonPin, keys[i].noteFreq);

        unsigned long audioStartedMs = millis();
        startKeyTone(i);
        audioStartedMs = millis();

        testLogLogManualPress(i, pressDetectedMs, audioStartedMs);
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
  // Ensure minimum note duration (50ms) so every note is audible
  unsigned long elapsed = millis() - toneStartTime[keyIndex];
  if (elapsed < MIN_NOTE_DURATION) {
    delay(MIN_NOTE_DURATION - elapsed);
  }

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
  if (!IS_VALID_KEY_INDEX(keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while turning on LEDs\n", keyIndex);
    return;
  }

  LOGF("[LED] Key %d LED ON: color=%s\n", keyIndex, getColorString(color));

  for (int i = 0; i < LEDS_PER_KEY; i++) {
    keys[keyIndex].led->setPixelColor(i, color);
  }

  keys[keyIndex].led->show();
}

// turns off all LEDs on a key's LED strip
void lightDownKey(int keyIndex) {
  if (!IS_VALID_KEY_INDEX(keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while turning off LEDs\n", keyIndex);
    return;
  }

  LOGF("[LED] Key %d OFF\n", keyIndex);

  for (int i = 0; i < LEDS_PER_KEY; i++) {
    keys[keyIndex].led->setPixelColor(i, 0);
  }

  keys[keyIndex].led->show();
}

// resets a key to its default state (LED off, servo at rest)
void resetKey(int keyIndex) {
  if (!IS_VALID_KEY_INDEX(keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while resetting key\n", keyIndex);
    return;
  }

  lightDownKey(keyIndex);
  autoReleaseKey(keyIndex);
  
  // Force clear the pressed state so the same key can be re-triggered
  // in the next step. Without this, consecutive steps on the same key
  // won't register as new presses because isPressed stays true.
  keys[keyIndex].isPressed = false;
}

void safeServoSetAngle(uint8_t channel, int angle) {
  int clampedAngle = constrain(angle, SERVO_MIN_SAFE_ANGLE, SERVO_MAX_SAFE_ANGLE);
  
  if (clampedAngle != angle) {
    LOGF("[WARN] Servo angle clamped: %d -> %d (valid: %d-%d)\n", 
         angle, clampedAngle, SERVO_MIN_SAFE_ANGLE, SERVO_MAX_SAFE_ANGLE);
  }
  
  servoDriver.setAngle(channel, clampedAngle);
}

// ============ VALIDATION & TESTING FUNCTIONS ============

bool validateSequenceData() {
  // Validate all sequences
  for (int s = 0; s < NUM_SEQUENCES; s++) {
    const Sequence& seq = sequences[s];
    
    if (seq.length <= 0 || seq.length > MAX_SEQUENCE_LENGTH) {
      LOGF("[ERROR] Sequence %d (%s) has invalid length: %d\n", s, seq.name, seq.length);
      return false;
    }
    
    for (int i = 0; i < seq.length; i++) {
      if (!IS_VALID_KEY_INDEX(seq.steps[i].keyIndex)) {
        LOGF("[ERROR] Sequence %d step %d has invalid keyIndex: %d\n", s, i, seq.steps[i].keyIndex);
        return false;
      }

      if (seq.steps[i].duration <= 0) {
        LOGF("[ERROR] Sequence %d step %d has invalid duration: %d\n", s, i, seq.steps[i].duration);
        return false;
      }
    }
  }

  return true;
}

bool validateHardwareInit() {
  if (NUM_KEYS <= 0) {
    LOGF("[ERROR] Invalid NUM_KEYS: %d", NUM_KEYS);
    return false;
  }

  if (NUM_SEQUENCES <= 0) {
    LOGF("[ERROR] Invalid NUM_SEQUENCES: %d", NUM_SEQUENCES);
    return false;
  }

  if (MAX_SEQUENCE_LENGTH <= 0) {
    LOGF("[ERROR] Invalid MAX_SEQUENCE_LENGTH: %d", MAX_SEQUENCE_LENGTH);
    return false;
  }

  if (LEDS_PER_KEY <= 0) {
    LOGF("[ERROR] Invalid LEDS_PER_KEY: %d", LEDS_PER_KEY);
    return false;
  }

  if (LED_BRIGHTNESS < 0 || LED_BRIGHTNESS > 255) {
    LOGF("[ERROR] Invalid LED_BRIGHTNESS: %d", LED_BRIGHTNESS);
    return false;
  }

  if (SERVO_FREQ <= 0 || SERVO_FREQ > 60) {
    LOGF("[ERROR] Invalid SERVO_FREQ: %d", SERVO_FREQ);
    return false;
  }

  if (SPEAKER_PIN < 2 || SPEAKER_PIN > 8) {
    LOGF("[ERROR] Invalid SPEAKER_PIN: %d", SPEAKER_PIN);
    return false;
  }

  for (int i = 0; i < NUM_KEYS; i++) {
    if (keys[i].buttonPin < 2 || keys[i].buttonPin > 8) {
      LOGF("[ERROR] Invalid buttonPin: %d for key %d", keys[i].buttonPin, i);
      return false;
    }
    if (keys[i].ledPin < 2 || keys[i].ledPin > 8) {
      LOGF("[ERROR] Invalid ledPin: %d for key %d", keys[i].ledPin, i);
      return false;
    }
    if (keys[i].servoChannel < 1 || keys[i].servoChannel > 16) {
      LOGF("[ERROR] Invalid servoChannel: %d for key %d", keys[i].servoChannel, i);
      return false;
    }
    if (keys[i].noteFreq <= 0 || keys[i].noteFreq > 4186) {
      LOGF("[ERROR] Invalid noteFreq: %d for key %d", keys[i].noteFreq, i);
      return false;
    }
  }

  return true;
}

void testLEDs() {
  LOGLN("[TEST] Testing LEDs...");
  
  // Flash all key LEDs white once, then off.
  for (int i = 0; i < NUM_KEYS; i++) {
    keys[i].led->setPixelColor(0, keys[i].led->Color(255, 255, 255));
    keys[i].led->show();
  }

  delay(300);

  for (int i = 0; i < NUM_KEYS; i++) {
    keys[i].led->setPixelColor(0, 0);
    keys[i].led->show();
  }

  LOGLN("[TEST] LED test complete.");
}

void testServos() {
  LOGLN("[TEST] Testing servos...");
  
  for (int i = 0; i < NUM_KEYS; i++) {    
    servoPull(keys[i].servoChannel);
    delay(500);
    servoRest(keys[i].servoChannel);
    delay(500);
  }
  
  LOGLN("[TEST] Servo test complete.");
}