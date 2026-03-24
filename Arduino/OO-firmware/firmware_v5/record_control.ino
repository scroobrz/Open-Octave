/*
===============================
      RECORDING CONTROL
===============================
Allows the user to record key presses into a sequence via a physical button
(RECORD_BUTTON_PIN on the PCA9555 I/O expander). Pressing the button toggles
recording on/off. While recording, key presses and their hold durations are
captured as sequence steps. Keys pressed within CHORD_WINDOW_MS of each other
are grouped into a single chord step (up to MAX_KEYS_PER_STEP keys).

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
  recStepCount = 0;
  recChordNumKeys = 0;
  LOGLN("\n[REC] ======== RECORDING STARTED ========");
  flashWhiteAnimation();
}

void stopRecording() {
  // Commit any in-progress chord before finalizing
  if (recChordNumKeys > 0) {
    commitRecordedStep();
  }

  recording = false;

  if (recStepCount > 0) {
    currentSequence.id = 0;
    strncpy(currentSequence.name, "Recorded", sizeof(currentSequence.name));
    currentSequence.length = recStepCount;
    // Steps were written directly into currentSequence.steps during recording

    LOGF("[REC] Recording saved: %d steps\n", recStepCount);
  } else {
    LOGLN("[REC] No notes recorded, sequence unchanged");
  }

  LOGLN("[REC] ======== RECORDING STOPPED ========\n");
  flashWhiteAnimation();
}

// Called when a key is pressed during recording.
// Starts a new chord or adds to the current one if within the chord window.
void recordKeyPress(int globalKey) {
  if (recStepCount >= MAX_SEQUENCE_LENGTH) {
    LOGLN("[REC] Max sequence length reached, stopping recording");
    stopRecording();
    return;
  }

  // No chord in progress — start a new one
  if (recChordNumKeys == 0) {
    recChordStartTime = millis();
    recChordKeys[0] = globalKey;
    recChordNumKeys = 1;
    return;
  }

  // Within chord window and room for more keys — add to current chord
  if (millis() - recChordStartTime < CHORD_WINDOW_MS && recChordNumKeys < MAX_KEYS_PER_STEP) {
    recChordKeys[recChordNumKeys] = globalKey;
    recChordNumKeys++;
    return;
  }

  // Past chord window or max keys per step — commit current step, start new one
  commitRecordedStep();

  if (recStepCount >= MAX_SEQUENCE_LENGTH) {
    LOGLN("[REC] Max sequence length reached, stopping recording");
    stopRecording();
    return;
  }

  recChordStartTime = millis();
  recChordKeys[0] = globalKey;
  recChordNumKeys = 1;
}

// Called when a key is released during recording.
// The first release in a chord commits the step (duration = press-to-first-release).
void recordKeyRelease(int globalKey) {
  if (recChordNumKeys == 0) return;

  // Check if this key is part of the current chord
  bool isInChord = false;
  for (uint8_t i = 0; i < recChordNumKeys; i++) {
    if (recChordKeys[i] == globalKey) {
      isInChord = true;
      break;
    }
  }

  if (!isInChord) return;

  // First release in the chord commits the step
  commitRecordedStep();
}

// Writes the current chord into currentSequence.steps and resets chord state.
void commitRecordedStep() {
  if (recChordNumKeys == 0 || recStepCount >= MAX_SEQUENCE_LENGTH) return;

  unsigned long duration = millis() - recChordStartTime;
  if (duration < MIN_STEP_DURATION) duration = MIN_STEP_DURATION;
  if (duration > MAX_STEP_DURATION) duration = MAX_STEP_DURATION;

  SequenceStep* step = &currentSequence.steps[recStepCount];
  step->numKeys = recChordNumKeys;
  step->duration = (uint16_t)duration;

  for (uint8_t i = 0; i < recChordNumKeys; i++) {
    step->keys[i] = recChordKeys[i];
    step->colors[i] = COLOR_PINK;
  }

  LOGF("[REC] Step %d: %d key(s), %dms\n", recStepCount + 1, recChordNumKeys, (int)duration);

  recStepCount++;
  recChordNumKeys = 0;
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

