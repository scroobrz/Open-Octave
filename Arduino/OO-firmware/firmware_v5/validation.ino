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

  if (MAX_SEQUENCE_LENGTH <= 0) {
    LOGF("[ERROR] Invalid MAX_SEQUENCE_LENGTH: %d", MAX_SEQUENCE_LENGTH);
    return false;
  }

  if (LEDS_PER_KEY <= 0) {
    LOGF("[ERROR] Invalid LEDS_PER_KEY: %d", LEDS_PER_KEY);
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

  if (SPEAKER_PIN < 0 || SPEAKER_PIN > 33) {
    LOGF("[ERROR] Invalid SPEAKER_PIN: %d", SPEAKER_PIN);
    return false;
  }

  for (int i = 0; i < NUM_KEYS; i++) {
    if (keys[i].buttonPin < 0 || keys[i].buttonPin > 15) {
      LOGF("[ERROR] Invalid buttonPin: %d for key %d", keys[i].buttonPin, i);
      return false;
    }
    if (keys[i].ledPin < 0 || keys[i].ledPin > 33) {
      LOGF("[ERROR] Invalid ledPin: %d for key %d", keys[i].ledPin, i);
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

void testLEDs() {
  LOGLN("[TEST] Testing LEDs...");

  for (int i = 0; i < NUM_KEYS; i++) {
    for (int j = 0; j < LEDS_PER_KEY; j++) {
      keys[i].led->setPixelColor(j, COLOR_WHITE);
    }
    keys[i].led->show();
  }

  delay(300);

  for (int i = 0; i < NUM_KEYS; i++) {
    for (int j = 0; j < LEDS_PER_KEY; j++) {
      keys[i].led->setPixelColor(j, 0);
    }
    keys[i].led->show();
  }

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
