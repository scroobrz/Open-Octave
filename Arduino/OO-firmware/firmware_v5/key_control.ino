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

        // startKeyTone(i);

        if (isMaster && recording) {
          recordKeyPress(globalKey);
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

      // stopKeyTone(i);

      if (isMaster && recording) {
        recordKeyRelease(globalKey);
      } else if (isMaster) {
        evaluateWrongKeyFeedback(globalKey, false);
      }
    }
  }
}

// starts playing the tone for a specific key - OUTDATED
inline void startKeyTone(int keyIndex) {
  tone(SPEAKER_PIN, keys[keyIndex].noteFreq);
}

// stops playing the tone for a specific key
// if another key is still pressed, switches to playing that key's tone instead
// (this handles the case where you have multiple keys held down)
// PROBLEM: it falls back to the pressed key with the lowest index rather than
// the one that was pressed last, could use a stack to solve this
void stopKeyTone(int keyIndex) {
  // check if any other key is still being pressed
  for (int i = 0; i < NUM_KEYS; i++) {
    if (keys[i].isPressed) {
      // found another pressed key, play its tone instead
      // (this cancels any sustain since a new key takes over)
      toneSustainKey = -1;
      startKeyTone(i);
      return;
    }
  }

  // no other keys pressed — check if we need to sustain for audibility
  unsigned long elapsed = millis() - toneStartTime[keyIndex];
  if (elapsed < MIN_TONE_DURATION) {
    // keep the tone playing non-blockingly until MIN_TONE_DURATION elapses
    toneSustainKey = keyIndex;
    toneSustainUntil = toneStartTime[keyIndex] + MIN_TONE_DURATION;
    return;
  }

  // tone has already played long enough, silence immediately
  noTone(SPEAKER_PIN);
}

// called from the main loop to silence tones that were sustained past key
// release for audibility. Non-blocking replacement for the old delay().
void handleToneSustain() {
  if (toneSustainKey < 0) return;

  // if the user pressed another key while sustaining, cancel —
  // startKeyTone will have already overridden the speaker
  if (keys[toneSustainKey].isPressed) {
    toneSustainKey = -1;
    return;
  }

  if (millis() >= toneSustainUntil) {
    noTone(SPEAKER_PIN);
    toneSustainKey = -1;
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

// Compute a smoothly interpolated brand-gradient color for a given key position.
// Blends across five stops: CYAN → GREEN → GOLD → CORAL → MAGENTA.
// Uses integer-only math (no floats) to keep it efficient on ESP32.
static uint32_t getBrandGradientColor(uint8_t keyIndex) {
  static const uint32_t stops[] = {
    COLOR_CYAN,     // 0x00B4D8
    COLOR_GREEN,    // 0x00FF00
    COLOR_GOLD,     // 0xFFD700
    COLOR_CORAL,    // 0xFF6B35
    COLOR_MAGENTA   // 0xE8368F
  };
  static const uint8_t NUM_STOPS = 5;
  static const uint8_t NUM_SEGMENTS = NUM_STOPS - 1;  // 4

  // Map keyIndex (0 to NUM_KEYS-1) into a fixed-point position across segments.
  // pos ranges from 0 to (NUM_SEGMENTS * 255).
  uint16_t pos = (uint16_t)keyIndex * NUM_SEGMENTS * 255 / (NUM_KEYS - 1);
  uint8_t segment = pos / 255;
  uint8_t blend = pos % 255;  // 0-254 blend factor within segment

  if (segment >= NUM_SEGMENTS) {
    segment = NUM_SEGMENTS - 1;
    blend = 255;
  }

  uint32_t c1 = stops[segment];
  uint32_t c2 = stops[segment + 1];

  uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
  uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;

  uint8_t r = ((uint32_t)r1 * (255 - blend) + (uint32_t)r2 * blend) / 255;
  uint8_t g = ((uint32_t)g1 * (255 - blend) + (uint32_t)g2 * blend) / 255;
  uint8_t b = ((uint32_t)b1 * (255 - blend) + (uint32_t)b2 * blend) / 255;

  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Plays a rainbow sweep from left to right when the keyboard powers on.
// Each key briefly shows its brand gradient color, then turns white.
// Once all keys are white, holds for STARTUP_WHITE_HOLD ms, then all LEDs off.
void playStartupAnimation() {
  LOGLN("[ANIM] Playing startup animation");

  for (int i = 0; i < NUM_KEYS; i++) {
    leds.setPixelColor(keys[i].ledIndex, getBrandGradientColor(i));
    leds.show();
    delay(SWEEP_DELAY_PER_KEY);
    leds.setPixelColor(keys[i].ledIndex, COLOR_WHITE);
    leds.show();
  }

  delay(STARTUP_WHITE_HOLD);

  for (int i = 0; i < NUM_KEYS; i++) {
    leds.setPixelColor(keys[i].ledIndex, 0);
  }
  leds.show();

  LOGLN("[ANIM] Startup animation complete");
}

// Plays a rainbow sweep from right to left when the keyboard powers off.
// All keys flash white briefly, then each key shows its brand gradient color
// before turning off, sweeping from right to left.
void playShutdownAnimation() {
  LOGLN("[ANIM] Playing shutdown animation");

  for (int i = 0; i < NUM_KEYS; i++) {
    leds.setPixelColor(keys[i].ledIndex, COLOR_WHITE);
  }
  leds.show();
  delay(SHUTDOWN_WHITE_HOLD);

  for (int i = NUM_KEYS - 1; i >= 0; i--) {
    leds.setPixelColor(keys[i].ledIndex, getBrandGradientColor(i));
    leds.show();
    delay(SWEEP_DELAY_PER_KEY);
    leds.setPixelColor(keys[i].ledIndex, 0);
    leds.show();
  }

  LOGLN("[ANIM] Shutdown animation complete");
}
