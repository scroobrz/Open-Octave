/*
===============================
   KEYBOARD CONTROL FUNCTIONS
===============================
These handle button detection, sound playback, and LED control for the keys.
*/

// checks all buttons and plays/stops tones based on their state
void handleKeyPresses() {
  // Read all 16 input pins in a single burst (2 I2C transactions total).
  // stateOfPin() then reads from the cached value — no further I2C traffic per key.
  ioport.pinStates();
  for (int i = 0; i < NUM_KEYS; i++) {
    int globalKey = (moduleChainIndex * NUM_KEYS) + i;
    bool buttonPressed = ioport.stateOfPin(keys[i].buttonPin) == HIGH;

    if (buttonPressed && !keys[i].isPressed) {

      // apply debouncing to avoid false triggers
      if (millis() - globalKeyPressTime[globalKey] >= DEBOUNCE_DELAY) {
        unsigned long pressDetectedMs = millis();

        // use local keys array to detect button press edges, and global array
        // for sequence handling.
        keys[i].isPressed = true;
        globalKeyIsPressed[globalKey] = true;
        globalKeyPressTime[globalKey] = pressDetectedMs;
        toneStartTime[i] = pressDetectedMs;  // Track when this tone started

        if (!isMaster) {
          chainSendCmd(UpstreamSerial, 'K', globalKey);
        }

        LOGF("[KEY] Key %d PRESSED (pin %d, freq %dHz)\n", i, keys[i].buttonPin, keys[i].noteFreq);

        startKeyTone(i);

        if (isMaster) {
          evaluateWrongKeyFeedback(globalKey, true);
        }

        unsigned long audioStartedMs = millis();
        testLogLogManualPress(i, pressDetectedMs, audioStartedMs);
      }

    } else if (!buttonPressed && keys[i].isPressed) {
      keys[i].isPressed = false;
      globalKeyIsPressed[globalKey] = false;

      if (!isMaster) {
        chainSendCmd(UpstreamSerial, 'k', globalKey);
      }
      
      LOGF("[KEY] Key %d RELEASED\n", i);

      stopKeyTone(i);

      if (isMaster) {
        evaluateWrongKeyFeedback(globalKey, false);
      }
    }
  }
}

// starts playing the tone for a specific key
inline void startKeyTone(int keyIndex) {
  tone(SPEAKER_PIN, keys[keyIndex].noteFreq);
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
// Clearing it prematurely caused phantom PRESSED events because the servo
// hadn't physically released yet, so handleKeyPresses() would see the button
// still HIGH with isPressed==false and register a ghost "new press".
// For consecutive same-key steps, the SERVO_RELEASE_DELAY provides enough
// time for the physical release + handleKeyPresses() to detect it.
void resetKey(int keyIndex) {
  if (!isValidLocalKeyIndex(keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while resetting key\n", keyIndex);
    return;
  }

  lightDownKey(keyIndex);
  autoReleaseKey(keyIndex);
}