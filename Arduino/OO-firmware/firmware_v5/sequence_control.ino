/*
===============================
       SEQUENCE HANDLING
===============================
*/

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
      executeNextSequenceStep();
    }
    return;
  }

  if (millis() - currentStepStartTime >= CURRENT_STEP.duration) {
    LOGF("[SEQ] Step %d/%d complete\n", currentSequenceStepIndex + 1, currentSequence.length);

    resetKey(CURRENT_STEP.keyIndex);

    currentSequenceStepIndex++;
    if (currentSequenceStepIndex >= currentSequence.length) {
      LOGLN("[SEQ] Sequence complete");
      stopSequence();
      return;
    }

    // Manually handle successive sequence steps by adding a delay to ensure proper movement up and down
    if (CURRENT_STEP.keyIndex == PREVIOUS_STEP.keyIndex) {
      waitingForServoRelease = true;
      servoReleaseStartTime = millis();
      // Don't execute step yet - will be done on next loop iteration after delay
    } else {
      executeNextSequenceStep();
    }
  }
}

void handleGuidedModePlayback() {
  // Use the later of (key press time, step start time) so that holds
  // that began before the current step don't count toward its duration.
  // This prevents instant-skip when the key is already held from a
  // previous step or from the user pre-pressing.
  unsigned long holdStart = lastKeyPressTime[CURRENT_STEP.keyIndex];
  if (holdStart < currentStepStartTime) {
    holdStart = currentStepStartTime;
  }

  if (millis() - holdStart >= CURRENT_STEP.duration &&
      keys[CURRENT_STEP.keyIndex].isPressed) {

    LOGF("[SEQ] Correct key %d pressed, advancing sequence.\n", CURRENT_STEP.keyIndex);
    resetKey(CURRENT_STEP.keyIndex);
    currentSequenceStepIndex++;

    if (currentSequenceStepIndex >= currentSequence.length) {
      LOGLN("[SEQ] Sequence complete");
      stopSequence();
      return;
    }

    // Manually handle successive sequence steps by adding a delay to ensure proper LED relighting
    if (CURRENT_STEP.keyIndex == PREVIOUS_STEP.keyIndex) {
      delay(80);
    }

    executeNextSequenceStep();
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
  executeNextSequenceStep();
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

  if (testLogEnabled) {
    testLogExpectedNextStepStartTime = 0;
    testLogLastAutoKey = -1;
    testLogAutoRepeatStreak = 0;
  }

  sequenceRunning = false;
  waitingForServoRelease = false;
}

// plays a single step of a sequence
void executeNextSequenceStep() {
  currentStepStartTime = millis();

  if (!isValidKeyIndex(CURRENT_STEP.keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while executing sequence step\n", CURRENT_STEP.keyIndex);
    testLogLogError(TESTLOG_INVALID_KEY_INDEX, F("ERROR_INVALID_KEY"));
    return;
  }

  LOGF("[SEQ] Step %d/%d: key=%d, color=%s, duration=%dms\n",
       currentSequenceStepIndex + 1, currentSequence.length,
       CURRENT_STEP.keyIndex, getColorString(CURRENT_STEP.color), 
       CURRENT_STEP.duration);

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
  lightUpKey(CURRENT_STEP.keyIndex, CURRENT_STEP.color);
  unsigned long ledCmdLatencyMs = millis() - ledCmdStart;

  unsigned long servoCmdLatencyMs = 0;
  // if we're in teaching mode, also press the key with the servo
  if (currentSequenceMode == TEACHING) {
    LOGF("[SERVO] Auto-pressing key %d (channel %d)\n", 
         CURRENT_STEP.keyIndex, keys[CURRENT_STEP.keyIndex].servoChannel);
    unsigned long servoCmdStart = millis();
    autoPressKey(CURRENT_STEP.keyIndex);
    servoCmdLatencyMs = millis() - servoCmdStart;
  }

  if (testLogEnabled) {
    int nextIndex = currentSequenceStepIndex + 1;
    bool nextIsSameKey = false;

    if (nextIndex >= 0 && nextIndex < currentSequence.length) {
      nextIsSameKey = (currentSequence.steps[nextIndex].keyIndex == CURRENT_STEP.keyIndex);
    }

    testLogLogAutoStep(CURRENT_STEP.keyIndex, autoplayTimingErrorMs, 
      ledCmdLatencyMs, servoCmdLatencyMs, (uint16_t)CURRENT_STEP.duration, 
      nextIsSameKey);
  }
}

// Loads a hardcoded default sequence into currentSequence.
// The melody is a simple ascending/descending scale across 3 keys:
void loadDefaultSequence() {
  currentSequence.id = 0;
  strcpy(currentSequence.name, "Default Scale");

  const SequenceStep defaultSteps[] = {
    {0, COLOR_RED,    500},   // C4 (key 0)
    {4, COLOR_GREEN,  500},   // E4 (key 4)
    {7, COLOR_BLUE,   500},   // G4 (key 7)
    {4, COLOR_GREEN,  500},   // E4 (key 4)
    {1, COLOR_RED,    500},   // C#4 (key 1)
    {9, COLOR_GREEN,  500},   // A4 (key 9)
    {8, COLOR_BLUE,   500},   // G#4 (key 8)
    {5, COLOR_GREEN,  500}    // F4 (key 5)
  };

  currentSequence.length = sizeof(defaultSteps) / sizeof(defaultSteps[0]);
  memcpy(currentSequence.steps, defaultSteps, sizeof(defaultSteps));
}
