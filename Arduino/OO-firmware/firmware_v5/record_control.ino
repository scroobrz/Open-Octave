/*
===============================
      RECORDING CONTROL
===============================
Allows the user to record key presses into a sequence via a physical button
(RECORD_BUTTON_PIN on the PCA9555 I/O expander). Pressing the button toggles
recording on/off. While recording, each key press is tracked independently:
press time is captured on the down-stroke and a single-key SequenceNote is
committed with the exact hold duration on the up-stroke. Keys still held when
recording stops are also committed, using millis() as the implied release time.

When recording stops, the captured steps replace currentSequence so the user
can immediately play back what they recorded using the guided/teaching buttons.
*/

void handleRecordButton() {
  if (sequenceRunning || uploadingSequence) return;

  static bool lastRecordState = false;
  bool recordState = (ioport.stateOfPin(RECORD_BUTTON_PIN) == HIGH);

  if (recordState && !lastRecordState && millis() - lastSequenceButtonPressTime >= BUTTON_DEBOUNCE_DELAY) {
    if (!recording) {
      startRecording();
    } else {
      stopRecording();
    }
    lastSequenceButtonPressTime = millis();
  }

  lastRecordState = recordState;
}

void startRecording() {
  recording = true;
  recNoteCount = 0;
  recStartTime = millis();          // NEW: epoch for note startTimes

  for (uint8_t i = 0; i < MAX_REC_ACTIVE_KEYS; i++) {
    recActiveKeys[i].active = false;
  }

  LOGLN("\n[REC] ======== RECORDING STARTED ========");
  flashWhiteAnimation();
}

void stopRecording() {
  for (uint8_t i = 0; i < MAX_REC_ACTIVE_KEYS; i++) {
    if (!recActiveKeys[i].active) continue;
    if (recNoteCount >= MAX_SEQUENCE_NOTES) break;

    unsigned long pressTime = recActiveKeys[i].pressTime;
    unsigned long duration  = millis() - pressTime;
    if (duration < MIN_STEP_DURATION) duration = MIN_STEP_DURATION;
    if (duration > MAX_STEP_DURATION) duration = MAX_STEP_DURATION;

    SequenceNote* note = &currentSequence.notes[recNoteCount];
    note->startTime = (uint32_t)(pressTime - recStartTime);
    note->duration  = (uint16_t)duration;
    note->globalKey = recActiveKeys[i].globalKey;
    note->_pad      = 0;
    note->color     = COLOR_PINK;

    LOGF("[REC] Note %d (flush): key=%d, t=%lu, %dms\n",
         recNoteCount + 1, recActiveKeys[i].globalKey,
         (unsigned long)note->startTime, (int)duration);
    recNoteCount++;
    recActiveKeys[i].active = false;
  }

  recording = false;

  if (recNoteCount > 0) {
    currentSequence.id = 0;
    strncpy(currentSequence.name, "Recorded", sizeof(currentSequence.name));

    // Keys may release in a different order than pressed, so notes can be out of
    // startTime order. The playback engine REQUIRES notes sorted by startTime
    // (guided-mode moment grouping depends on it). Insertion sort (fine for <=512).
    for (uint16_t a = 1; a < recNoteCount; a++) {
      SequenceNote key = currentSequence.notes[a];
      int b = a - 1;
      while (b >= 0 && currentSequence.notes[b].startTime > key.startTime) {
        currentSequence.notes[b + 1] = currentSequence.notes[b];
        b--;
      }
      currentSequence.notes[b + 1] = key;
    }

    currentSequence.noteCount = recNoteCount;
    LOGF("[REC] Recording saved: %d notes\n", recNoteCount);
  } else {
    LOGLN("[REC] No notes recorded, sequence unchanged");
  }

  LOGLN("[REC] ======== RECORDING STOPPED ========\n");
  flashWhiteAnimation();
}

// Called when a key is pressed during recording.
// Finds a free slot in recActiveKeys and stores the press timestamp.
void recordKeyPress(int globalKey) {
  if (recNoteCount >= MAX_SEQUENCE_NOTES) {
    LOGLN("[REC] Max sequence length reached, stopping recording");
    stopRecording();
    return;
  }

  // Guard against duplicate press events for the same key
  for (uint8_t i = 0; i < MAX_REC_ACTIVE_KEYS; i++) {
    if (recActiveKeys[i].active && recActiveKeys[i].globalKey == (uint8_t)globalKey) {
      LOGF("[REC] Key %d already tracked, ignoring duplicate press\n", globalKey);
      return;
    }
  }

  // Allocate a free slot
  for (uint8_t i = 0; i < MAX_REC_ACTIVE_KEYS; i++) {
    if (!recActiveKeys[i].active) {
      recActiveKeys[i].globalKey = (uint8_t)globalKey;
      recActiveKeys[i].pressTime = millis();
      recActiveKeys[i].active    = true;
      LOGF("[REC] Key %d pressed, tracking in slot %d\n", globalKey, i);
      return;
    }
  }

  // All slots full — more than MAX_REC_ACTIVE_KEYS keys held simultaneously
  LOGF("[REC] Tracking table full (%d slots), key %d ignored\n", MAX_REC_ACTIVE_KEYS, globalKey);
}

// Called when a key is released during recording.
// Finds the matching slot, commits a single-key SequenceNote with the hold
// duration, and frees the slot.
void recordKeyRelease(int globalKey) {
  for (uint8_t i = 0; i < MAX_REC_ACTIVE_KEYS; i++) {
    if (!recActiveKeys[i].active || recActiveKeys[i].globalKey != (uint8_t)globalKey) continue;

    if (recNoteCount >= MAX_SEQUENCE_NOTES) {
      LOGLN("[REC] Max sequence length reached, stopping recording");
      recActiveKeys[i].active = false;
      stopRecording();
      return;
    }

    unsigned long pressTime = recActiveKeys[i].pressTime;
    unsigned long duration  = millis() - pressTime;
    if (duration < MIN_STEP_DURATION) duration = MIN_STEP_DURATION;
    if (duration > MAX_STEP_DURATION) duration = MAX_STEP_DURATION;

    SequenceNote* note = &currentSequence.notes[recNoteCount];
    note->startTime = (uint32_t)(pressTime - recStartTime);
    note->duration  = (uint16_t)duration;
    note->globalKey = (uint8_t)globalKey;
    note->_pad      = 0;
    note->color     = COLOR_PINK;

    LOGF("[REC] Note %d: key=%d, t=%lu, %dms\n",
         recNoteCount + 1, globalKey,
         (unsigned long)note->startTime, (int)duration);

    recNoteCount++;
    recActiveKeys[i].active = false;
    return;
  }

  // Key not found in tracking table — spurious release, safe to ignore
  LOGF("[REC] Release for untracked key %d ignored\n", globalKey);
}

// Brief white flash across all LEDs across all modules to signal recording start/stop.
void flashWhiteAnimation() {
  // Flash master module
  for (int i = 0; i < NUM_KEYS; i++) {
    leds.setPixelColor(keys[i].ledIndex, COLOR_WHITE);
  }
  leds.show();

  // Flash downstream modules
  for (int m = 1; m < numModulesInChain; m++) {
    for (int i = 0; i < NUM_KEYS; i++) {
      int targetGlobalKey = m * NUM_KEYS + i;
      chainSendKeyCmdWithColor(DownstreamSerial, 'g', targetGlobalKey, COLOR_WHITE);
    }
  }

  delay(RECORD_FLASH_HOLD);

  // Turn off master module
  for (int i = 0; i < NUM_KEYS; i++) {
    leds.setPixelColor(keys[i].ledIndex, 0);
  }
  leds.show();

  // Turn off downstream modules
  DownstreamSerial.write("x\n", 2);
}
