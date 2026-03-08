/*
===============================
    MODE CONTROL FUNCTIONS
===============================
*/

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
  if (currentSequenceStepIndex < 0 || currentSequenceStepIndex >= getCurrentSequence().length) {
    LOGF("[ERROR] Invalid step index: %d encountered while handling automatic modes\n", currentSequenceStepIndex);
    testLogLogError(TESTLOG_INVALID_STEP_INDEX, F("ERROR_INVALID_STEP_INDEX"));
    stopSequence();
    return;
  }

  if (currentMode == TEACHING) {
    handleTeachingMode();
  } else if (currentMode == GUIDED) {
    handleGuidedMode();
  }
}

void handleTeachingMode() {
  // If we're waiting for the servo to release (between consecutive same-key steps)
  if (waitingForServoRelease) {
    if (millis() - servoReleaseStartTime >= SERVO_RELEASE_DELAY) {
      waitingForServoRelease = false;
      executeNextSequenceStep();
    }
    return;
  }

  if (millis() - currentStepStartTime >= getCurrentSequenceStep().duration) {
    LOGF("[SEQ] Step %d/%d complete\n", currentSequenceStepIndex + 1, getCurrentSequence().length);

    uint8_t previousKeyIndex = getCurrentSequenceStep().keyIndex;
    resetKey(previousKeyIndex);

    currentSequenceStepIndex++;
    if (currentSequenceStepIndex >= getCurrentSequence().length) {
      LOGLN("[SEQ] Sequence complete");
      stopSequence();
      return;
    }

    // Manually handle successive sequence steps by adding a delay to ensure proper movement up and down
    if (getCurrentSequenceStep().keyIndex == previousKeyIndex) {
      waitingForServoRelease = true;
      servoReleaseStartTime = millis();
      // Don't execute step yet - will be done on next loop iteration after delay
    } else {
      executeNextSequenceStep();
    }
  }
}

void handleGuidedMode() {
  uint8_t previousKeyIndex = getCurrentSequenceStep().keyIndex;

  if (millis() - lastKeyPressTime[previousKeyIndex] >= getCurrentSequenceStep().duration &&
      keyJustPressed == previousKeyIndex) {

    LOGF("[SEQ] Correct key %d pressed, advancing sequence.\n", keyJustPressed);
    keyJustPressed = -1;
    resetKey(previousKeyIndex);
    currentSequenceStepIndex++;

    if (currentSequenceStepIndex >= getCurrentSequence().length) {
      LOGLN("[SEQ] Sequence complete");
      stopSequence();
      return;
    }

    // Manually handle successive sequence steps by adding a delay to ensure proper LED relighting
    if (getCurrentSequenceStep().keyIndex == previousKeyIndex) {
      delay(80);
    }

    executeNextSequenceStep();
  }
}
