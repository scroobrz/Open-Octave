/*
===============================
         HELPERS
===============================
*/

void powerOn(){
  if (on) return;
  playStartupAnimation();
  on = true;
  emitStatus();
}

void powerOff(){
  if (!on) return;
  if (recording) stopRecording();
  stopSequence();
  playShutdownAnimation();
  on = false;
  emitStatus();
}

bool checkUpstream(){
  pinMode(RX1, INPUT_PULLDOWN);
  delay(10);
  bool hasUpstream = digitalRead(RX1) == HIGH;
  pinMode(RX1, INPUT);

  return hasUpstream;
}

void safeServoSetAngle(uint8_t channel, uint16_t angle) {
  uint16_t clampedAngle = constrain(angle, SERVO_MIN_SAFE_ANGLE, SERVO_MAX_SAFE_ANGLE);

  if (clampedAngle != angle) {
    LOGF("[WARN] Servo angle clamped: %d -> %d (valid: %d-%d)\n",
         angle, clampedAngle, SERVO_MIN_SAFE_ANGLE, SERVO_MAX_SAFE_ANGLE);
  }

  servoDriver.setAngle(channel, clampedAngle);
}

void wsSendLog(const char* msg) {
  if (!wsReady) return;  // WebSocket not yet initialized
  webSocket.sendTXT(msg);
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

const char *getCurrentSequenceModeString() {
  switch (currentSequenceMode) {
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
  case COLOR_MAGENTA:
    return "MAGENTA";
  default:
    return "CUSTOM";
  }
}

// Compute a smoothly interpolated gradient color for a given key position.
// Blends across five finger-colour stops: BLUE → GREEN → YELLOW → ORANGE → MAGENTA.
// Uses integer-only math (no floats) to keep it efficient on ESP32.
static uint32_t getBrandGradientColor(uint8_t keyIndex) {
  static const uint32_t stops[] = {
    COLOR_BLUE,     // 0x0000FF
    COLOR_GREEN,    // 0x00FF00
    COLOR_YELLOW,   // 0xFFFF00
    COLOR_ORANGE,   // 0xFF8000
    COLOR_MAGENTA   // 0xFF00FF
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

void toLowercase(char &c) {
  if (c >= 'A' && c <= 'Z') {
    c = c + ('a' - 'A');
  }
}

void emitStatus() {
  LOGF("STATUS power=%d running=%d seq=%d step=%d mode=%s\n",
       on, sequenceRunning, currentSequence.id, 
       (sequenceRunning ? currentSequenceStepIndex : -1), 
       (sequenceRunning ? getCurrentSequenceModeString() : "N/A"));
}
