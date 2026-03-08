/*
===============================
       SEQUENCE HANDLING
===============================
*/

// Loads a hardcoded default sequence into currentSequence.
// This gives the firmware a ready-to-play demo so Guided and Teaching modes
// work out of the box without needing a sequence upload from the controller.
// The melody is a simple ascending/descending scale across all 3 keys:
//   C4 → E4 → G4 → E4  (repeated twice, 500ms per note)
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

// starts playing the sequence from the beginning
void startSequence() {
  if (sequenceRunning) {
    LOGLN("[SEQ] Sequence already running, ignoring start request");
    return;
  }

  if (getCurrentSequence().length <= 0) {
    LOGF("[ERROR] Invalid sequence length: %d encountered while starting sequence\n", getCurrentSequence().length);
    return;
  }

  LOGLN("\n[SEQ] ======== STARTING SEQUENCE ========");
  LOGF("[SEQ] Sequence: %s (%d steps)\n", getCurrentSequence().name, getCurrentSequence().length);
  LOGLN("EVT sequence_started");
  emitStatus();

  sequenceRunning = true;
  currentSequenceStepIndex = 0;
  currentStepStartTime = millis();

  if (testLogEnabled) {
    testLogExpectedNextStepStartTime = currentStepStartTime;
    testLogLastAutoKey = -1;
    testLogAutoRepeatStreak = 0;
  }

  // immediately play the first step
  executeSequenceStep(getCurrentSequenceStep());
}

// stops the sequence and turns off all keys
void stopSequence() {
  if (!sequenceRunning) return;  // Nothing to stop

  LOGLN("\n[SEQ] ======== STOPPING SEQUENCE ========");
  LOGF("[SEQ] Total steps completed: %d/%d\n", currentSequenceStepIndex, getCurrentSequence().length);
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
void executeSequenceStep(const SequenceStep &step) {
  currentStepStartTime = millis();

  if (!isValidKeyIndex(step.keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while executing sequence step\n", step.keyIndex);
    testLogLogError(TESTLOG_INVALID_KEY_INDEX, F("ERROR_INVALID_KEY"));
    return;
  }

  LOGF("[SEQ] Step %d/%d: key=%d, color=%s, duration=%dms\n",
       currentSequenceStepIndex + 1, getCurrentSequence().length,
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
  // if we're in teaching mode, also press the key with the servo
  if (currentMode == TEACHING) {
    LOGF("[SERVO] Auto-pressing key %d (channel %d)\n", step.keyIndex, keys[step.keyIndex].servoChannel);
    unsigned long servoCmdStart = millis();
    autoPressKey(step.keyIndex);
    servoCmdLatencyMs = millis() - servoCmdStart;
  }

  if (testLogEnabled) {
    int nextIndex = currentSequenceStepIndex + 1;
    bool nextIsSameKey = false;
    if (nextIndex >= 0 && nextIndex < getCurrentSequence().length) {
      nextIsSameKey = (getCurrentSequence().steps[nextIndex].keyIndex == step.keyIndex);
    }
    testLogLogAutoStep(step.keyIndex, autoplayTimingErrorMs, ledCmdLatencyMs, servoCmdLatencyMs, (uint16_t)step.duration, nextIsSameKey);
  }
}
