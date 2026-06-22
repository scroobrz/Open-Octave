/*
===============================
   KEYBOARD CONTROL FUNCTIONS
===============================
These handle button detection, sound playback, and LED control for the keys.
*/

// checks all buttons and plays/stops tones based on their state
void handleKeyPresses() {
  for (int i = 0; i < NUM_KEYS; i++) {
    int globalKey = (moduleChainIndex * NUM_KEYS) + i;
    bool buttonPressed = ioport.stateOfPin(keys[i].buttonPin) == HIGH;

    if (buttonPressed && !keys[i].isPressed) {

      // apply debouncing to avoid false triggers
      if (millis() - globalKeyPressTime[globalKey] >= BUTTON_DEBOUNCE_DELAY) {
        unsigned long pressDetectedMs = millis();

        // use local keys array to detect button press edges, and global array
        // for sequence handling.
        keys[i].isPressed = true;
        globalKeyIsPressed[globalKey] = true;
        globalKeyPressTime[globalKey] = pressDetectedMs;
        toneStartTime[i] = pressDetectedMs;  // Track when this tone started

        if (!isMaster) {
          chainSendKeyCmd(UpstreamSerial, 'K', globalKey);
        }

        LOGF("[KEY] Key %d PRESSED (pin %d, freq %dHz)\n", i, keys[i].buttonPin, keys[i].noteFreq);

        if (isMaster && recording) {
          recordKeyPress(globalKey);
        } else if (isMaster && sequenceRunning && currentSequenceMode == BROADCAST) {
          for (int m = 1; m < numModulesInChain; m++) {
            int slaveGlobalKey = (m * NUM_KEYS) + i;
            chainSendKeyCmdWithColor(DownstreamSerial, 't', slaveGlobalKey, getBrandGradientColor(i));
          }
        } else if (isMaster) {
          evaluateWrongKeyFeedback(globalKey, true);
        }

        unsigned long audioStartedMs = millis();
        testLogLogManualPress(i, pressDetectedMs, audioStartedMs);
      }

    } else if (!buttonPressed && keys[i].isPressed) {
      keys[i].isPressed = false;
      globalKeyIsPressed[globalKey] = false;

      if (!isMaster) {
        chainSendKeyCmd(UpstreamSerial, 'k', globalKey);
      }
      
      LOGF("[KEY] Key %d RELEASED\n", i);

      if (isMaster && recording) {
        recordKeyRelease(globalKey);
      } else if (isMaster && sequenceRunning && currentSequenceMode == BROADCAST) {
        for (int m = 1; m < numModulesInChain; m++) {
          int slaveGlobalKey = (m * NUM_KEYS) + i;
          chainSendKeyCmd(DownstreamSerial, 'r', slaveGlobalKey);
        }
      } else if (isMaster) {
        evaluateWrongKeyFeedback(globalKey, false);
      }
    }
  }
}

// lights up all LEDs on a key's LED strip with the specified color
void lightUpKey(int keyIndex, uint32_t color) {
  if (!isValidLocalKeyIndex(keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while turning on LEDs\n", keyIndex);
    return;
  }

  leds.setPixelColor(keys[keyIndex].ledIndex, color);
  leds.show();
  LOGF("[LED] Key %d LED ON: color=%s\n", keyIndex, getColorString(color));
}

// turns off all LEDs on a key's LED strip
void lightDownKey(int keyIndex) {
  if (!isValidLocalKeyIndex(keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while turning off LEDs\n", keyIndex);
    return;
  }

  leds.setPixelColor(keys[keyIndex].ledIndex, 0);
  leds.show();
  LOGF("[LED] Key %d OFF\n", keyIndex);
}

// resets a key to its default state (LED off, servo at rest)
// NOTE: We do NOT clear isPressed here. handleKeyPresses() tracks the physical
// button state and will detect the actual release when the servo lets go.
void resetKey(int keyIndex) {
  if (!isValidLocalKeyIndex(keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while resetting key\n", keyIndex);
    return;
  }

  lightDownKey(keyIndex);
  autoReleaseKey(keyIndex);
}

// ============ STARTUP / SHUTDOWN ANIMATION ============

// Plays a rainbow sweep from left to right when the keyboard powers on.
// Each key lights up with its brand gradient color in sequence, holds briefly, then all LEDs off.
void playStartupAnimation() {
  LOGLN("[ANIM] Playing startup animation");

  for (int i = 0; i < NUM_KEYS; i++) {
    leds.setPixelColor(keys[i].ledIndex, getBrandGradientColor(i));
    leds.show();
    delay(SWEEP_DELAY_PER_KEY);
  }

  delay(STARTUP_WHITE_HOLD);

  for (int i = 0; i < NUM_KEYS; i++) {
    leds.setPixelColor(keys[i].ledIndex, 0);
  }
  leds.show();

  LOGLN("[ANIM] Startup animation complete");
}

// Plays a rainbow sweep from right to left when the keyboard powers off.
// All keys light up with their brand gradient colors, hold briefly,
// then turn off one by one from right to left.
void playShutdownAnimation() {
  LOGLN("[ANIM] Playing shutdown animation");

  for (int i = 0; i < NUM_KEYS; i++) {
    leds.setPixelColor(keys[i].ledIndex, getBrandGradientColor(i));
  }
  leds.show();
  delay(SHUTDOWN_WHITE_HOLD);

  for (int i = NUM_KEYS - 1; i >= 0; i--) {
    leds.setPixelColor(keys[i].ledIndex, 0);
    leds.show();
    delay(SWEEP_DELAY_PER_KEY);
  }

  LOGLN("[ANIM] Shutdown animation complete");
}
