/*
===============================
       SEQUENCE HANDLING
===============================
*/

void handleSequenceButtons() {
  if (sequenceRunning || recording) return;

  // Suppress button triggers for 500ms after promotion to master.
  // The static debounce variables below don't update while the module is a
  // slave (handleSequenceButtons isn't called by slaves), so on the first
  // call after promotion, any held or bouncing button would register as a
  // rising edge and auto-start a sequence.
  if (millis() - promotionSuppressionTime < 500) return;

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
  if (!sequenceRunning || currentSequenceMode == BROADCAST)
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
      playSuccessAnimation();
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

  if (mode != BROADCAST && currentSequence.length <= 0) {
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

  // immediately play the first step (if not in broadcast mode)
  if (currentSequenceMode != BROADCAST) {
    executeCurrentSequenceStep();
  } else {
    char bCmd[16];
    snprintf(bCmd, sizeof(bCmd), "b%d\n", currentOctave);
    DownstreamSerial.write((uint8_t*)bCmd, strlen(bCmd));
  }
}

// stops the sequence and turns off all keys
void stopSequence() {
  if (!sequenceRunning) return;  // Nothing to stop

  LOGLN("\n[SEQ] ======== STOPPING SEQUENCE ========");
  LOGF("[SEQ] Total steps completed: %d/%d\n", currentSequenceStepIndex, currentSequence.length);
  LOGLN("EVT sequence_complete");

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
  
  if (currentSequenceMode == BROADCAST) {
    configureNotes();
  }
  
  emitStatus();
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

  // A-minor descending melody — distinct from the two-octave C-major version
  // Key map: 0=C4 1=C#4 2=D4 4=E4 5=F4 7=G4 8=G#4 9=A4 10=A#4 11=B4
  // Avoids keys 3 (D#4) and 6 (F#4)
  const SequenceStep defaultSteps[] = {
    {3, {9, 0, 4},    {COLOR_ORANGE, COLOR_BLUE, COLOR_YELLOW},     800},  // A minor chord (A4+C4+E4)
    {1, {7},           {COLOR_MAGENTA},                               400},  // G4 melody
    {1, {5},           {COLOR_ORANGE},                                400},  // F4 melody
    {1, {4},           {COLOR_YELLOW},                                400},  // E4 melody
    {2, {2, 9},        {COLOR_GREEN, COLOR_ORANGE},                   600},  // D4+A4 fifth
    {1, {5},           {COLOR_ORANGE},                                400},  // F4 melody
    {1, {4},           {COLOR_YELLOW},                                400},  // E4 melody
    {3, {2, 5, 9},     {COLOR_GREEN, COLOR_ORANGE, COLOR_ORANGE},    800},  // D minor chord (D4+F4+A4)
    {1, {0},           {COLOR_BLUE},                                  400},  // C4 melody
    {1, {2},           {COLOR_GREEN},                                 400},  // D4 melody
    {3, {4, 8, 11},    {COLOR_YELLOW, COLOR_VIOLET, COLOR_INDIGO},   800},  // E major chord (E4+G#4+B4)
    {3, {9, 0, 4},     {COLOR_ORANGE, COLOR_BLUE, COLOR_YELLOW},    1000},  // A minor resolve
  };

  currentSequence.length = sizeof(defaultSteps) / sizeof(defaultSteps[0]);
  memcpy(currentSequence.steps, defaultSteps, sizeof(defaultSteps));
}

// Loads a two-octave default sequence spanning keys 0-23 (modules 0 and 1).
// Key mapping: Module 0 = C4(0)..B4(11), Module 1 = C5(12)..B5(23)
void loadDefaultSequenceTwoOctave() {
  currentSequence.id = 0;
  strcpy(currentSequence.name, "Default (2-Oct)");

  const SequenceStep defaultSteps[] = {
    {4, {0, 4, 7, 12},    {COLOR_BLUE, COLOR_YELLOW, COLOR_MAGENTA, COLOR_BLUE},       800},  // C major w/ octave
    {1, {14},              {COLOR_GREEN},                                                 400},  // D5 melody
    {1, {16},              {COLOR_YELLOW},                                                400},  // E5 melody
    {4, {5, 9, 12, 17},   {COLOR_ORANGE, COLOR_BLUE, COLOR_BLUE, COLOR_ORANGE},         800},  // F major w/ octave
    {1, {19},              {COLOR_MAGENTA},                                               400},  // G5 melody
    {1, {16},              {COLOR_YELLOW},                                                400},  // E5 melody
    {4, {0, 7, 12, 19},   {COLOR_BLUE, COLOR_MAGENTA, COLOR_BLUE, COLOR_MAGENTA},       600},  // C+G fifths across octaves
    {4, {7, 11, 14, 19},  {COLOR_MAGENTA, COLOR_ORANGE, COLOR_GREEN, COLOR_MAGENTA},    800},  // G major w/ octave
    {1, {17},              {COLOR_ORANGE},                                                400},  // F5 melody
    {1, {16},              {COLOR_YELLOW},                                                400},  // E5 melody
    {1, {14},              {COLOR_GREEN},                                                 400},  // D5 melody
    {4, {0, 4, 7, 12},    {COLOR_BLUE, COLOR_YELLOW, COLOR_MAGENTA, COLOR_BLUE},       1000},  // C major resolve w/ octave
  };

  currentSequence.length = sizeof(defaultSteps) / sizeof(defaultSteps[0]);
  memcpy(currentSequence.steps, defaultSteps, sizeof(defaultSteps));
}

// Switches the default sequence to match the current chain size.
// Only acts if the current sequence is the default (id == 0) and no sequence is running.
void updateDefaultSequenceForChainSize() {
  if (currentSequence.id != 0 || sequenceRunning) return;

  if (numModulesInChain >= 2) {
    loadDefaultSequenceTwoOctave();
  } else {
    loadDefaultSequence();
  }

  LOGF("[SEQ] Default sequence updated for %d module(s): \"%s\" (%d steps)\n",
       numModulesInChain, currentSequence.name, currentSequence.length);
}
