/*
===============================
       SEQUENCE HANDLING
===============================
*/

void handleSequenceButtons() {
  if (sequenceRunning || recording) return;

  static bool lastGuidedState = false;
  static bool lastTeachingState = false;

  bool guidedState = (ioport.stateOfPin(GUIDED_SEQ_BUTTON_PIN) == HIGH);
  bool teachingState = (ioport.stateOfPin(TEACHING_SEQ_BUTTON_PIN) == HIGH);

  if (guidedState && !lastGuidedState && millis() - lastSequenceButtonPressTime >= BUTTON_DEBOUNCE_DELAY) {
    startSequence(GUIDED);
    lastSequenceButtonPressTime = millis();
  } else if (teachingState && !lastTeachingState && millis() - lastSequenceButtonPressTime >= BUTTON_DEBOUNCE_DELAY) {
    startSequence(TEACHING);
    lastSequenceButtonPressTime = millis();
  }

  lastGuidedState = guidedState;
  lastTeachingState = teachingState;
}

// handles automatic sequence playback
void handleSequencePlayback() {
  if (!sequenceRunning)
    return;

  // Defensive check: ensure currentSequenceStep is valid
  if (currentSequenceStepIndex < 0 || currentSequenceStepIndex >= currentSequence.length) {
    LOGF("[ERROR] Invalid step index: %d encountered while handling automatic modes\n", currentSequenceStepIndex);
    testLogLogError(TESTLOG_INVALID_STEP_INDEX, F("ERROR_INVALID_STEP_INDEX"));
    stopSequence();
    return;
  }

  if (currentSequenceMode == TEACHING) {
    handleTeachingModePlayback();
  } else if (currentSequenceMode == GUIDED) {
    handleGuidedModePlayback();
  }
}

void handleTeachingModePlayback() {
  // If we're waiting for the servo to release (between consecutive same-key steps)
  if (waitingForServoRelease) {
    if (millis() - servoReleaseStartTime >= SERVO_RELEASE_DELAY) {
      waitingForServoRelease = false;
      executeCurrentSequenceStep();
    }
    return;
  }

  if (millis() - currentStepStartTime >= CURRENT_STEP.duration) {
    LOGF("[SEQ] Step %d/%d complete\n", currentSequenceStepIndex + 1, currentSequence.length);

    bool requiresServoDelay = false;

    for (int i = 0; i < CURRENT_STEP.numKeys; i++) {
      uint8_t keyIndex = CURRENT_STEP.keys[i];
      uint8_t moduleIndexForStep = keyIndex / NUM_KEYS;

      if (moduleIndexForStep > 0) {
        chainSendKeyCmd(DownstreamSerial, 'r', keyIndex);
      } else {
        resetKey(keyIndex);
      }

      int nextIndex = currentSequenceStepIndex + 1;
      if (nextIndex < currentSequence.length) {
        for (int j = 0; j < currentSequence.steps[nextIndex].numKeys; j++) {
          if (currentSequence.steps[nextIndex].keys[j] == keyIndex) {
            requiresServoDelay = true;
          }
        }
      }
    }

    currentSequenceStepIndex++;
    if (currentSequenceStepIndex >= currentSequence.length) {
      LOGLN("[SEQ] Sequence complete");
      stopSequence();
      return;
    }

    if (requiresServoDelay) {
      waitingForServoRelease = true;
      servoReleaseStartTime = millis();
    } else {
      executeCurrentSequenceStep();
    }
  }
}

void handleGuidedModePlayback() {
  bool allRequiredKeysPressed = true;
  unsigned long maxHoldStart = currentStepStartTime;

  // Check if all required keys are currently pressed and find the latest press time
  for (int i = 0; i < CURRENT_STEP.numKeys; i++) {
    uint8_t keyIndex = CURRENT_STEP.keys[i];

    if (!globalKeyIsPressed[keyIndex]) {
      allRequiredKeysPressed = false;
      break;
    }

    // Use the later of (key press time, step start time) so that holds
    // that began before the current step don't count toward its duration.
    unsigned long holdStart = globalKeyPressTime[keyIndex];
    if (holdStart < currentStepStartTime) {
      holdStart = currentStepStartTime;
    }

    // Keep track of the latest press time among all required keys
    if (holdStart > maxHoldStart) {
      maxHoldStart = holdStart;
    }
  }

  // Evaluate if all keys have been held for the required duration
  if (allRequiredKeysPressed && (millis() - maxHoldStart >= CURRENT_STEP.duration)) {
    LOGF("[SEQ] All correct keys pressed for step %d, advancing sequence.\n", currentSequenceStepIndex + 1);

    // Reset LEDs / send release commands for all keys in the current step
    for (int i = 0; i < CURRENT_STEP.numKeys; i++) {
      uint8_t keyIndex = CURRENT_STEP.keys[i];
      uint8_t moduleIndexForStep = keyIndex / NUM_KEYS;

      if (moduleIndexForStep > 0) {
        chainSendKeyCmd(DownstreamSerial, 'r', keyIndex);
      } else {
        resetKey(keyIndex);
      }
    }

    currentSequenceStepIndex++;
    if (currentSequenceStepIndex >= currentSequence.length) {
      LOGLN("[SEQ] Sequence complete");
      stopSequence();
      return;
    }

    delay(80); // delay to ensure visible LED relighting
    executeCurrentSequenceStep();
  }
}

// starts playing the sequence from the beginning
void startSequence(SequenceMode mode) {
  if (sequenceRunning) {
    LOGLN("[SEQ] Sequence already running, ignoring start request");
    return;
  }

  if (currentSequence.length <= 0) {
    LOGF("[ERROR] Invalid sequence length: %d encountered while starting sequence\n", currentSequence.length);
    return;
  }

  sequenceRunning = true;
  currentSequenceStepIndex = 0;
  currentStepStartTime = millis();
  currentSequenceMode = mode;

  LOGLN("\n[SEQ] ======== STARTING SEQUENCE ========");
  LOGF("[SEQ] Sequence: %s (%d steps)\n", currentSequence.name, currentSequence.length);
  LOGF("[SEQ] Mode: %s\n", getCurrentSequenceModeString());
  LOGLN("EVT sequence_started");
  emitStatus();

  if (testLogEnabled) {
    testLogExpectedNextStepStartTime = currentStepStartTime;
    testLogLastAutoKey = -1;
    testLogAutoRepeatStreak = 0;
  }

  // immediately play the first step
  executeCurrentSequenceStep();
}

// stops the sequence and turns off all keys
void stopSequence() {
  if (!sequenceRunning) return;  // Nothing to stop

  LOGLN("\n[SEQ] ======== STOPPING SEQUENCE ========");
  LOGF("[SEQ] Total steps completed: %d/%d\n", currentSequenceStepIndex, currentSequence.length);
  LOGLN("EVT sequence_complete");
  emitStatus();

  for (int i = 0; i < NUM_KEYS; i++) {
    resetKey(i);
  }

  DownstreamSerial.write("x\n", 2);

  if (testLogEnabled) {
    testLogExpectedNextStepStartTime = 0;
    testLogLastAutoKey = -1;
    testLogAutoRepeatStreak = 0;
  }

  sequenceRunning = false;
  waitingForServoRelease = false;
}

// plays a single step of a sequence
void executeCurrentSequenceStep() {
  currentStepStartTime = millis();

  for (int i = 0; i < CURRENT_STEP.numKeys; i++) {
    if (!isValidGlobalKeyIndex(CURRENT_STEP.keys[i])) {
      LOGF("[ERROR] Invalid keyIndex: %d encountered while executing sequence step\n", CURRENT_STEP.keys[i]);
      continue;
    }

    uint8_t moduleIndexForKey = CURRENT_STEP.keys[i] / NUM_KEYS;
    
    if (moduleIndexForKey > 0){
      LOGF("[SEQ] Forwarding step %d/%d along chain\n", currentSequenceStepIndex + 1, currentSequence.length);
      char cmd = (currentSequenceMode == GUIDED) ? 'g' : 't';
      chainSendKeyCmdWithColor(DownstreamSerial, cmd, CURRENT_STEP.keys[i], CURRENT_STEP.colors[i]);

    } else {
      LOGF("[SEQ] Step %d/%d: key=%d, color=%s, duration=%dms\n",
       currentSequenceStepIndex + 1, currentSequence.length,
       CURRENT_STEP.keys[i], getColorString(CURRENT_STEP.colors[i]), 
       CURRENT_STEP.duration);

      // light up the key's LED with the specified color
      lightUpKey(CURRENT_STEP.keys[i], CURRENT_STEP.colors[i]);

      // if we're in teaching mode, also press the key with the servo
      if (currentSequenceMode == TEACHING) {
        LOGF("[SERVO] Auto-pressing key %d (channel %d)\n", 
             CURRENT_STEP.keys[i], keys[CURRENT_STEP.keys[i]].servoChannel);
        autoPressKey(CURRENT_STEP.keys[i]);
      }
    }
  }
}

void evaluateWrongKeyFeedback(int globalKey, bool isPressed) {
  if (sequenceRunning && currentSequenceMode == GUIDED) {
    bool isCorrectKey = false;
    for (int i = 0; i < CURRENT_STEP.numKeys; i++) {
      if (globalKey == CURRENT_STEP.keys[i]) {
        isCorrectKey = true;
        break;
      }
    }

    if (!isCorrectKey) {
      uint8_t targetModule = globalKey / NUM_KEYS;
      int localKey = globalKey % NUM_KEYS;

      if (isPressed) {
        if (targetModule == 0) {
          lightUpKey(localKey, COLOR_RED);
        } else {
          chainSendKeyCmdWithColor(DownstreamSerial, 'g', globalKey, COLOR_RED);
        }
      } else {
        if (targetModule == 0) {
          lightDownKey(localKey);
        } else {
          chainSendKeyCmd(DownstreamSerial, 'r', globalKey);
        }
      }
    }
  }
}

// Loads a hardcoded default sequence into currentSequence.
void loadDefaultSequence() {
  currentSequence.id = 0;
  strcpy(currentSequence.name, "Default");

  // Short demo mixing single melody notes and chords
  const SequenceStep defaultSteps[] = {
    {3, {0, 4, 7},   {COLOR_CYAN, COLOR_GOLD, COLOR_MAGENTA},  800},  // C major chord
    {1, {4},          {COLOR_GOLD},                              400},  // E4 melody
    {1, {5},          {COLOR_CORAL},                             400},  // F4 melody
    {3, {5, 9, 0},    {COLOR_CORAL, COLOR_CYAN, COLOR_CYAN},    800},  // F major chord
    {1, {7},          {COLOR_MAGENTA},                           400},  // G4 melody
    {1, {4},          {COLOR_GOLD},                              400},  // E4 melody
    {2, {0, 7},       {COLOR_CYAN, COLOR_MAGENTA},               600},  // C4+G4 fifth
    {3, {7, 11, 2},   {COLOR_MAGENTA, COLOR_CORAL, COLOR_GREEN}, 800},  // G major chord
    {1, {2},          {COLOR_GREEN},                             400},  // D4 melody
    {3, {0, 4, 7},    {COLOR_CYAN, COLOR_GOLD, COLOR_MAGENTA}, 1000},  // C major chord resolve
  };

  currentSequence.length = sizeof(defaultSteps) / sizeof(defaultSteps[0]);
  memcpy(currentSequence.steps, defaultSteps, sizeof(defaultSteps));
}
