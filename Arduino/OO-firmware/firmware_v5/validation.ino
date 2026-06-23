/*
===============================
   VALIDATION & TESTING
===============================
*/

bool validateHardwareInit() {
  if (NUM_KEYS <= 0) {
    LOGF("[ERROR] Invalid NUM_KEYS: %d", NUM_KEYS);
    return false;
  }

  if (MAX_SEQUENCE_NOTES <= 0) {
    LOGF("[ERROR] Invalid MAX_SEQUENCE_NOTES: %d", MAX_SEQUENCE_NOTES);
    return false;
  }

  if (LED_BRIGHTNESS < 0 || LED_BRIGHTNESS > 255) {
    LOGF("[ERROR] Invalid LED_BRIGHTNESS: %d", LED_BRIGHTNESS);
    return false;
  }

  if (SERVO_FREQ <= 0 || SERVO_FREQ > 60) {
    LOGF("[ERROR] Invalid SERVO_FREQ: %d", SERVO_FREQ);
    return false;
  }

  for (int i = 0; i < NUM_KEYS; i++) {
    if (keys[i].buttonPin < 0 || keys[i].buttonPin > 15) {
      LOGF("[ERROR] Invalid buttonPin: %d for key %d", keys[i].buttonPin, i);
      return false;
    }
    if (keys[i].ledIndex < 0 || keys[i].ledIndex >= NUM_KEYS) {
      LOGF("[ERROR] Invalid ledIndex: %d for key %d", keys[i].ledIndex, i);
      return false;
    }
    if (keys[i].servoChannel < 1 || keys[i].servoChannel > 16) {
      LOGF("[ERROR] Invalid servoChannel: %d for key %d", keys[i].servoChannel, i);
      return false;
    }
    if (keys[i].noteFreq <= 0 || keys[i].noteFreq > 4186) {
      LOGF("[ERROR] Invalid noteFreq: %d for key %d", keys[i].noteFreq, i);
      return false;
    }
  }

  return true;
}

inline bool isValidLocalKeyIndex(int keyIndex) {
  return (keyIndex >= 0 && keyIndex < NUM_KEYS);
}

inline bool isValidGlobalKeyIndex(int keyIndex) {
  return (keyIndex >= 0 && keyIndex < (NUM_KEYS * numModulesInChain));
}

void testLEDs() {
  LOGLN("[TEST] Testing LEDs...");

  for (int i = 0; i < NUM_KEYS; i++) {
    leds.setPixelColor(keys[i].ledIndex, COLOR_WHITE);
  }
  leds.show();

  delay(300);

  for (int i = 0; i < NUM_KEYS; i++) {
    leds.setPixelColor(keys[i].ledIndex, 0);
  }
  leds.show();

  LOGLN("[TEST] LED test complete.");
}

void testServos() {
  LOGLN("[TEST] Testing servos...");

  for (int i = 0; i < NUM_KEYS; i++) {
    servoPull(keys[i].servoChannel);
    delay(500);
    servoRest(keys[i].servoChannel);
    delay(500);
  }

  LOGLN("[TEST] Servo test complete.");
}
