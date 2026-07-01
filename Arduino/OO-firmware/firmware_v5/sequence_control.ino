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

// Activates one key for a note: lights its LED (and, in TEACHING, presses the servo).
// Forwards to downstream modules for keys >= NUM_KEYS.
void activateNoteKey(uint8_t globalKey, uint32_t color, SequenceMode mode) {
  if (!isValidGlobalKeyIndex(globalKey)) {
    LOGF("[ERROR] Invalid keyIndex: %d while activating note\n", globalKey);
    return;
  }
  uint8_t module = globalKey / NUM_KEYS;
  if (module > 0) {
    char cmd = (mode == GUIDED) ? 'g' : 't';
    chainSendKeyCmdWithColor(DownstreamSerial, cmd, globalKey, color);
  } else {
    lightUpKey(globalKey, color);          // globalKey == local index for module 0
    if (mode == TEACHING) autoPressKey(globalKey);
  }
}

// Releases one key for a note: LED off + servo rest. Forwards downstream as needed.
void releaseNoteKey(uint8_t globalKey) {
  uint8_t module = globalKey / NUM_KEYS;
  if (module > 0) {
    chainSendKeyCmd(DownstreamSerial, 'r', globalKey);
  } else {
    resetKey(globalKey);                   // globalKey == local index for module 0
  }
}

void handleSequencePlayback() {
  if (!sequenceRunning || currentSequenceMode == BROADCAST) return;

  if (currentSequence.noteCount == 0) {
    LOGLN("[ERROR] Sequence has no notes; stopping");
    testLogLogError(TESTLOG_INVALID_STEP_INDEX, F("ERROR_EMPTY_SEQUENCE"));
    stopSequence();
    return;
  }

  if (currentSequenceMode == TEACHING)      handleTeachingModePlayback();
  else if (currentSequenceMode == GUIDED)   handleGuidedModePlayback();
}

void handleTeachingModePlayback() {
  unsigned long now = millis();
  unsigned long elapsed = now - sequenceStartTime;

  // 1. RELEASE: end any active note whose hold time has elapsed.
  for (uint16_t i = 0; i < currentSequence.noteCount; i++) {
    if (!noteActive[i]) continue;
    const SequenceNote &n = currentSequence.notes[i];
    if (elapsed >= (unsigned long)n.startTime + n.duration) {
      releaseNoteKey(n.globalKey);
      noteActive[i] = false;
      keyLastReleaseTime[n.globalKey] = now;
      notesCompleted++;
      LOGF("[SEQ] Note %d released (key=%d)\n", i, n.globalKey);
    }
  }

  // 2. START: begin any not-yet-started note whose startTime has arrived.
  for (uint16_t i = 0; i < currentSequence.noteCount; i++) {
    if (noteStarted[i]) continue;
    const SequenceNote &n = currentSequence.notes[i];
    if (elapsed < n.startTime) continue;

    // Servo-release guard: if this key was released < SERVO_RELEASE_DELAY ms ago,
    // defer so the servo physically resets before re-pressing. Retried next loop.
    if (now - keyLastReleaseTime[n.globalKey] < SERVO_RELEASE_DELAY) continue;

    activateNoteKey(n.globalKey, n.color, TEACHING);
    noteStarted[i] = true;
    noteActive[i]  = true;

    if (testLogEnabled) {
      long timingError = (long)((long)elapsed - (long)n.startTime); // ms late vs schedule
      testLogLogAutoNote(n.globalKey, timingError, now, now);
    }
    LOGF("[SEQ] Note %d started (key=%d, t=%lu, dur=%d)\n",
         i, n.globalKey, (unsigned long)n.startTime, n.duration);
  }

  // 3. DONE
  if (notesCompleted >= currentSequence.noteCount) {
    LOGLN("[SEQ] Sequence complete");
    stopSequence();
  }
}

// Lights the LEDs for the moment beginning at guidedMomentStart and records its
// size and required hold duration. A "moment" = the contiguous run of notes whose
// startTime falls within GUIDED_MOMENT_TOLERANCE of the first (notes are sorted by
// startTime), so a human-recorded chord's slightly-staggered presses group as one.
void presentGuidedMoment() {
  uint32_t momentTime = currentSequence.notes[guidedMomentStart].startTime;
  guidedMomentSize = 0;
  guidedMomentHoldDuration = 0;

  for (uint16_t i = guidedMomentStart;
       i < currentSequence.noteCount &&
       currentSequence.notes[i].startTime <= momentTime + GUIDED_MOMENT_TOLERANCE; i++) {
    const SequenceNote &n = currentSequence.notes[i];
    activateNoteKey(n.globalKey, n.color, GUIDED);   // LED only (no servo in guided)
    guidedMomentSize++;
    if (n.duration > guidedMomentHoldDuration) guidedMomentHoldDuration = n.duration;
  }

  guidedMomentPresentedTime = millis();
  guidedMomentPresented = true;
  LOGF("[SEQ] Presenting guided moment: %d note(s) at t=%lu, hold=%dms\n",
       guidedMomentSize, (unsigned long)momentTime, guidedMomentHoldDuration);
}

void handleGuidedModePlayback() {
  if (!guidedMomentPresented) { presentGuidedMoment(); return; }

  // All moment keys must be held simultaneously for the longest note's duration.
  bool allPressed = true;
  unsigned long latestHoldStart = guidedMomentPresentedTime;

  for (uint16_t i = guidedMomentStart; i < guidedMomentStart + guidedMomentSize; i++) {
    uint8_t gk = currentSequence.notes[i].globalKey;
    if (!globalKeyIsPressed[gk]) { allPressed = false; break; }

    // Clamp to presentation time so a key held from a PREVIOUS moment doesn't
    // pre-satisfy this one (independent-moments behaviour).
    unsigned long holdStart = globalKeyPressTime[gk];
    if (holdStart < guidedMomentPresentedTime) holdStart = guidedMomentPresentedTime;
    if (holdStart > latestHoldStart) latestHoldStart = holdStart;
  }

  if (allPressed && (millis() - latestHoldStart >= guidedMomentHoldDuration)) {
    LOGF("[SEQ] Guided moment complete (notes %d..%d)\n",
         guidedMomentStart, guidedMomentStart + guidedMomentSize - 1);

    for (uint16_t i = guidedMomentStart; i < guidedMomentStart + guidedMomentSize; i++) {
      releaseNoteKey(currentSequence.notes[i].globalKey);
    }

    guidedMomentStart += guidedMomentSize;
    notesCompleted    += guidedMomentSize;

    if (guidedMomentStart >= currentSequence.noteCount) {
      LOGLN("[SEQ] Sequence complete");
      stopSequence();
      return;
    }

    guidedMomentPresented = false;
    delay(80);  // brief gap so the next moment's relight is visible (matches old code)
  }
}

void startSequence(SequenceMode mode) {
  if (sequenceRunning) {
    LOGLN("[SEQ] Sequence already running, ignoring start request");
    return;
  }

  if (mode != BROADCAST && currentSequence.noteCount == 0) {
    LOGLN("[ERROR] Empty sequence; cannot start");
    return;
  }

  sequenceRunning = true;
  currentSequenceMode = mode;
  sequenceStartTime = millis();
  notesCompleted = 0;

  for (uint16_t i = 0; i < currentSequence.noteCount; i++) {
    noteStarted[i] = false;
    noteActive[i]  = false;
  }
  for (uint8_t k = 0; k < MAX_TOTAL_KEYS; k++) keyLastReleaseTime[k] = 0;

  guidedMomentStart = 0;
  guidedMomentSize = 0;
  guidedMomentPresented = false;

  LOGLN("\n[SEQ] ======== STARTING SEQUENCE ========");
  LOGF("[SEQ] Sequence: %s (%d notes)\n", currentSequence.name, currentSequence.noteCount);
  LOGF("[SEQ] Mode: %s\n", getCurrentSequenceModeString());
  LOGLN("EVT sequence_started");
  emitStatus();

  if (testLogEnabled) {
    testLogLastAutoKey = -1;
    testLogAutoRepeatStreak = 0;
  }

  if (currentSequenceMode == BROADCAST) {
    DownstreamSerial.write("b\n", 2);
  }
  // TEACHING / GUIDED: the first notes/moment trigger on the next
  // handleSequencePlayback() iteration (elapsed >= 0 / moment not yet presented).
}

void stopSequence() {
  if (!sequenceRunning) return;

  LOGLN("\n[SEQ] ======== STOPPING SEQUENCE ========");
  LOGF("[SEQ] Notes completed: %d/%d\n", notesCompleted, currentSequence.noteCount);
  LOGLN("EVT sequence_complete");

  // Release any notes still sounding.
  for (uint16_t i = 0; i < currentSequence.noteCount; i++) {
    if (noteActive[i]) {
      releaseNoteKey(currentSequence.notes[i].globalKey);
      noteActive[i] = false;
    }
  }

  for (int i = 0; i < NUM_KEYS; i++) resetKey(i);   // defensive local reset
  DownstreamSerial.write("x\n", 2);

  if (testLogEnabled) {
    testLogLastAutoKey = -1;
    testLogAutoRepeatStreak = 0;
  }

  sequenceRunning = false;
  notesCompleted = 0;
  guidedMomentPresented = false;
  guidedMomentStart = 0;
  guidedMomentSize = 0;

  if (currentSequenceMode == BROADCAST) configureNotes();
  emitStatus();
}

void evaluateWrongKeyFeedback(int globalKey, bool isPressed) {
  if (sequenceRunning && currentSequenceMode == GUIDED && guidedMomentPresented) {
    bool isCorrectKey = false;
    for (uint16_t i = guidedMomentStart; i < guidedMomentStart + guidedMomentSize; i++) {
      if (globalKey == currentSequence.notes[i].globalKey) { isCorrectKey = true; break; }
    }

    if (!isCorrectKey) {
      uint8_t targetModule = globalKey / NUM_KEYS;
      int localKey = globalKey % NUM_KEYS;
      if (isPressed) {
        if (targetModule == 0) lightUpKey(localKey, COLOR_RED);
        else chainSendKeyCmdWithColor(DownstreamSerial, 'g', globalKey, COLOR_RED);
      } else {
        if (targetModule == 0) lightDownKey(localKey);
        else chainSendKeyCmd(DownstreamSerial, 'r', globalKey);
      }
    }
  }
}

void loadDefaultSequence() {
  currentSequence.id = 0;
  strcpy(currentSequence.name, "Default");

  // A-minor descending melody. Keys: 0=C4 2=D4 4=E4 5=F4 7=G4 8=G#4 9=A4 11=B4
  const SequenceNote defaultNotes[] = {
    // A minor chord (A4+C4+E4) @ t0, 800ms
    {   0, 800,  9, 0, COLOR_ORANGE}, {   0, 800,  0, 0, COLOR_BLUE},   {   0, 800,  4, 0, COLOR_YELLOW},
    { 800, 400,  7, 0, COLOR_MAGENTA},                                   // G4
    {1200, 400,  5, 0, COLOR_ORANGE},                                    // F4
    {1600, 400,  4, 0, COLOR_YELLOW},                                    // E4
    {2000, 600,  2, 0, COLOR_GREEN},  {2000, 600,  9, 0, COLOR_ORANGE},  // D4+A4 fifth
    {2600, 400,  5, 0, COLOR_ORANGE},                                    // F4
    {3000, 400,  4, 0, COLOR_YELLOW},                                    // E4
    // D minor chord (D4+F4+A4) @ t3400, 800ms
    {3400, 800,  2, 0, COLOR_GREEN},  {3400, 800,  5, 0, COLOR_ORANGE},  {3400, 800,  9, 0, COLOR_ORANGE},
    {4200, 400,  0, 0, COLOR_BLUE},                                      // C4
    {4600, 400,  2, 0, COLOR_GREEN},                                     // D4
    // E major chord (E4+G#4+B4) @ t5000, 800ms
    {5000, 800,  4, 0, COLOR_YELLOW}, {5000, 800,  8, 0, COLOR_VIOLET},  {5000, 800, 11, 0, COLOR_INDIGO},
    // A minor resolve (A4+C4+E4) @ t5800, 1000ms
    {5800,1000,  9, 0, COLOR_ORANGE}, {5800,1000,  0, 0, COLOR_BLUE},    {5800,1000,  4, 0, COLOR_YELLOW},
  };

  currentSequence.noteCount = sizeof(defaultNotes) / sizeof(defaultNotes[0]); // 21
  memcpy(currentSequence.notes, defaultNotes, sizeof(defaultNotes));
}

void loadDefaultSequenceTwoOctave() {
  currentSequence.id = 0;
  strcpy(currentSequence.name, "Default (2-Oct)");

  const SequenceNote defaultNotes[] = {
    // C major w/ octave (0,4,7,12) @ t0, 800
    {   0, 800,  0, 0, COLOR_BLUE},   {   0, 800,  4, 0, COLOR_YELLOW},  {   0, 800,  7, 0, COLOR_MAGENTA}, {   0, 800, 12, 0, COLOR_BLUE},
    { 800, 400, 14, 0, COLOR_GREEN},                                     // D5
    {1200, 400, 16, 0, COLOR_YELLOW},                                    // E5
    // F major w/ octave (5,9,12,17) @ t1600, 800
    {1600, 800,  5, 0, COLOR_ORANGE}, {1600, 800,  9, 0, COLOR_BLUE},    {1600, 800, 12, 0, COLOR_BLUE},   {1600, 800, 17, 0, COLOR_ORANGE},
    {2400, 400, 19, 0, COLOR_MAGENTA},                                   // G5
    {2800, 400, 16, 0, COLOR_YELLOW},                                    // E5
    // C+G fifths across octaves (0,7,12,19) @ t3200, 600
    {3200, 600,  0, 0, COLOR_BLUE},   {3200, 600,  7, 0, COLOR_MAGENTA}, {3200, 600, 12, 0, COLOR_BLUE},   {3200, 600, 19, 0, COLOR_MAGENTA},
    // G major w/ octave (7,11,14,19) @ t3800, 800
    {3800, 800,  7, 0, COLOR_MAGENTA},{3800, 800, 11, 0, COLOR_ORANGE},  {3800, 800, 14, 0, COLOR_GREEN},  {3800, 800, 19, 0, COLOR_MAGENTA},
    {4600, 400, 17, 0, COLOR_ORANGE},                                    // F5
    {5000, 400, 16, 0, COLOR_YELLOW},                                    // E5
    {5400, 400, 14, 0, COLOR_GREEN},                                     // D5
    // C major resolve w/ octave (0,4,7,12) @ t5800, 1000
    {5800,1000,  0, 0, COLOR_BLUE},   {5800,1000,  4, 0, COLOR_YELLOW},  {5800,1000,  7, 0, COLOR_MAGENTA},{5800,1000, 12, 0, COLOR_BLUE},
  };

  currentSequence.noteCount = sizeof(defaultNotes) / sizeof(defaultNotes[0]); // 27
  memcpy(currentSequence.notes, defaultNotes, sizeof(defaultNotes));
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

  LOGF("[SEQ] Default sequence updated for %d module(s): \"%s\" (%d notes)\n",
       numModulesInChain, currentSequence.name, currentSequence.noteCount);
}
