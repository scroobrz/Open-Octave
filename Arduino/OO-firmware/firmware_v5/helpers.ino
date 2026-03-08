/*
===============================
         HELPERS
===============================
*/

void safeServoSetAngle(uint8_t channel, uint16_t angle) {
  uint16_t clampedAngle = constrain(angle, SERVO_MIN_SAFE_ANGLE, SERVO_MAX_SAFE_ANGLE);

  if (clampedAngle != angle) {
    LOGF("[WARN] Servo angle clamped: %d -> %d (valid: %d-%d)\n",
         angle, clampedAngle, SERVO_MIN_SAFE_ANGLE, SERVO_MAX_SAFE_ANGLE);
  }

  servoDriver.setAngle(channel, clampedAngle);
}

void wsBroadcastLog(const char* msg) {
  if (!wsReady) return;  // WebSocket not yet initialized
  webSocket.broadcastTXT(msg);
}

inline const Sequence& getCurrentSequence() {
  return currentSequence;
}

inline const SequenceStep& getCurrentSequenceStep() {
  return currentSequence.steps[currentSequenceStepIndex];
}

inline void servoPull(int channel) {
  safeServoSetAngle(channel, SERVO_PRESS_ANGLE);
}

inline void servoRest(int channel) {
  safeServoSetAngle(channel, SERVO_REST_ANGLE);
}

inline void autoPressKey(int keyIndex) {
  servoPull(keys[keyIndex].servoChannel);
}

inline void autoReleaseKey(int keyIndex) {
  servoRest(keys[keyIndex].servoChannel);
}

inline bool isValidKeyIndex(int keyIndex) {
  return (keyIndex >= 0 && keyIndex < NUM_KEYS);
}

const char *getCurrentModeString() {
  switch (currentMode) {
  case MANUAL:
    return "MANUAL";
  case GUIDED:
    return "GUIDED";
  case TEACHING:
    return "TEACHING";
  default:
    return "UNKNOWN";
  }
}

const char *getColorString(uint32_t color) {
  switch (color) {
  case COLOR_RED:
    return "RED";
  case COLOR_ORANGE:
    return "ORANGE";
  case COLOR_YELLOW:
    return "YELLOW";
  case COLOR_GREEN:
    return "GREEN";
  case COLOR_BLUE:
    return "BLUE";
  case COLOR_INDIGO:
    return "INDIGO";
  case COLOR_VIOLET:
    return "VIOLET";
  case COLOR_WHITE:
    return "WHITE";
  default:
    return "CUSTOM";
  }
}

void toLowercase(char &c) {
  if (c >= 'A' && c <= 'Z') {
    c = c + ('a' - 'A');
  }
}

void emitStatus() {
  LOGF("STATUS mode=%s running=%d seq=%d step=%d\n",
       getCurrentModeString(), sequenceRunning,
       currentSequence.id, (sequenceRunning ? currentSequenceStepIndex : -1));
}
