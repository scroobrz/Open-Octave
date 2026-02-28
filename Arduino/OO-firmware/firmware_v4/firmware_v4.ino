/*
 * FIRMWARE V4
 *
 * This firmware currently controls 3 keys across the following three modes:
 *   - MANUAL: User plays keys manually, no automation
 *   - GUIDED: Guided mode - LEDs light up, user must press key to advance
 *   - TEACHING: LEDs + servos play automatically (no user input needed)
 *
 * Sound is always triggered by button presses - whether the user presses a key
 * or a servo pulls it down, the button underneath is what triggers the sound.
 *
 * NETWORKING:
 *   The ESP32 hosts a WiFi Access Point and runs a WebSocket server on port 81.
 *   Commands are received as single-character text messages (same format as USB
 *   serial commands), and all log output is broadcast back to connected clients.
 *   This "serial-over-WebSocket" design keeps the firmware simple and makes the
 *   WiFi interface behave identically to the USB serial interface. Sequences are
 *   uploaded through a specialised protocol; these are the only commands
 *   that are not single-character.
 */

#include "Arduino.h"
#include "PCA9685.h"
#include "firmware_V4_config.h"
#include "firmware_V4_debug.h"
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <cstdint>

// ============ FUNCTION PROTOTYPES ============

void setupWiFi();
void handleWiFiStatus();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void handleSequenceCommand(char *str);
void processSequenceUploadCommand(char *str);
void processSequenceEndCommand(char *str);
bool processSequenceStepCommand(uint8_t stepIndex, char *str);
void handleSerialCommands();
void processSerialCommand(char cmd);
void setMode(Mode mode);
void handleAutomaticModes();
void handleGuidedMode();
void handleTeachingMode();
void startSequence();
void stopSequence();
void executeSequenceStep(const SequenceStep &step);
void startKeyTone(int keyIndex);
void stopKeyTone(int keyIndex);
void handleKeyPresses();
void lightUpKey(int keyIndex, uint32_t color);
void lightDownKey(int keyIndex);
void resetKey(int keyIndex);
void safeServoSetAngle(uint8_t servoChannel, uint16_t angle);
bool validateSequenceData();
bool validateHardwareInit();
void testLEDs();
void testServos();
const Sequence& getCurrentSequence();
const SequenceStep& getCurrentSequenceStep();
void servoPull(int channel);
void servoRest(int channel);
void autoPressKey(int keyIndex);
void autoReleaseKey(int keyIndex);
bool isValidKeyIndex(int keyIndex);
const char *getCurrentModeString();
const char *getColorString(uint32_t color);
void toLowercase(char &c);
void wsBroadcastLog(const char* msg);
void testLogPrintHeader();
void testLogStart();
void testLogStop();
void testLogLogManualPress(int keyIndex, unsigned long pressDetectedMs, unsigned long audioStartedMs);
void testLogLogAutoStep(int keyIndex, long timingErrorMs, unsigned long ledCmdMs, unsigned long servoCmdMs, uint16_t stepDurationMs, bool nextIsSameKey);
void testLogLogError(uint8_t errorCode, const __FlashStringHelper* eventType);

// ============ HARDWARE DEFINITIONS ============

Key keys[NUM_KEYS] = {
    {KEY0_BUTTON_PIN, KEY0_LED_PIN, nullptr, KEY0_SERVO_CHANNEL, KEY0_NOTE, false}, // C4
    {KEY1_BUTTON_PIN, KEY1_LED_PIN, nullptr, KEY1_SERVO_CHANNEL, KEY1_NOTE, false}, // D4
    {KEY2_BUTTON_PIN, KEY2_LED_PIN, nullptr, KEY2_SERVO_CHANNEL, KEY2_NOTE, false}  // E4
};

ServoDriver servoDriver;
WebSocketsServer webSocket(81);

// ============ GLOBAL STATE ============

Mode currentMode = MANUAL;
Sequence currentSequence;

bool uploadingSequence = false;
uint8_t uploadStepCount = 0;
int uploadingSequenceId = -1;
Sequence uploadSequenceBuffer;

bool sequenceRunning = false;
int currentSequenceStepIndex = 0;
unsigned long currentStepStartTime = 0;

unsigned long lastKeyPressTime[NUM_KEYS] = {0};
unsigned long toneStartTime[NUM_KEYS] = {0};

bool waitingForServoRelease = false;
unsigned long servoReleaseStartTime = 0;

bool testLogEnabled = false;
uint16_t testLogRunId = 0;
uint16_t testLogEventId = 0;
int8_t testLogLastManualKey = -1;
unsigned long testLogLastManualTime = 0;
uint8_t testLogManualRepeatStreak = 0;
unsigned long testLogExpectedNextStepStartTime = 0;
int8_t testLogLastAutoKey = -1;
uint8_t testLogAutoRepeatStreak = 0;

unsigned long lastWifiCheckTime = 0;
bool isWifiConnected = false;
bool wsReady = false;  // Prevents wsBroadcastLog() from running before webSocket.begin()

char serialBuf[SERIAL_BUF_SIZE];
uint8_t serialBufPos = 0;
bool serialBufOverflow = false;  // true = discard bytes until next newline

// Tracks the most recently pressed key index in the current loop (or -1 if none)
int keyJustPressed = -1;

/*
===============================
     CORE ARDUINO FUNCTIONS
===============================
These are the two functions that Arduino calls automatically.
*/

// runs once
void setup() {
  LOG_INIT();
  LOGLN("\n========================================");
  LOGLN("    OPEN OCTAVE FIRMWARE V4 - INIT");
  LOGLN("========================================");

  // ===== VALIDATION =====
  LOG("[SETUP] Validating hardware config... ");
  if (!validateHardwareInit()) {
    LOGLN("[ERROR] CRITICAL: Hardware validation failed!");
    LOGLN("System halted. Check configuration.");
    while (true) {
      delay(1000);
    } // Halt - configuration error
  }
  LOGLN("OK");

  LOGLN("[SETUP] Initializing sequence memory... ");
  currentSequence.length = 0;
  strcpy(currentSequence.name, "Empty Sequence");

  // ===== INITIALIZATION =====
  LOG("[SETUP] Configuring speaker... ");
  pinMode(SPEAKER_PIN, OUTPUT);
  noTone(SPEAKER_PIN);
  LOGF("OK (speaker_pin: %d)\n", SPEAKER_PIN);

  LOG("[SETUP] Initializing I2C... ");
  Wire.begin();
  LOGLN("OK");

  LOG("[SETUP] Initializing servo driver... ");
  servoDriver.init();
  servoDriver.setFrequency(SERVO_FREQ);
  LOGF("OK (freq: %dHz)\n", SERVO_FREQ);

  // initialize each key
  LOGLN("[SETUP] Initializing keys:");
  for (int i = 0; i < NUM_KEYS; i++) {
    pinMode(keys[i].buttonPin, INPUT);
    servoRest(keys[i].servoChannel);

    // Create NeoPixel object dynamically (can't be done at global scope)
    keys[i].led = new Adafruit_NeoPixel(LEDS_PER_KEY, keys[i].ledPin, NEO_GRB + NEO_KHZ800);
    keys[i].led->begin();
    keys[i].led->setBrightness(LED_BRIGHTNESS);
    keys[i].led->show();
    keys[i].isPressed = false;

    LOGF("  Key %d: btn_pin=%d, led_pin=%d, servo_ch=%d, freq=%dHz\n", i,
         keys[i].buttonPin, keys[i].ledPin, keys[i].servoChannel,
         keys[i].noteFreq);
  }
  LOGF("OK (%d keys initialized)\n", NUM_KEYS);

  LOGLN("[SETUP] Connecting to WiFi...");
  setupWiFi();
  LOGLN("[SETUP] WiFi & WebSocket Active!");

  LOGLN("========================================");
  LOGF("[SETUP] Complete! Starting in %s mode\n", getCurrentModeString());
  LOGLN("========================================\n");
}

// runs repeatedly forever
void loop() {
  keyJustPressed = -1;     // Reset key press tracking for this loop
  webSocket.loop();        // handle WebSocket events
  handleSerialCommands();  // handle serial commands
  handleKeyPresses();      // detect any key presses and play sounds
  handleWiFiStatus();      // check wifi connection state

  // if we're in an automatic mode, handle the sequence playback
  if (currentMode != MANUAL) {
    handleAutomaticModes();
  }
}

/*
===============================
           API
===============================
The firmware exposes a command API over two transports:

  1. USB Serial — characters arrive one byte at a time via the hardware
     UART (Serial.read). handleSerialCommands() accumulates them into a
     line buffer until a newline is received, then dispatches the
     complete line. Log output is printed to Serial.

  2. WiFi WebSocket — the ESP32 hosts a WiFi Access Point and runs a
     WebSocket server on port 81. Each WebSocket text message is
     delivered as a complete payload, so no line buffering is needed.
     All log output is broadcast back to every connected client,
     creating a "serial-over-WiFi" experience.

Two kinds of commands are supported:

  • Single-character commands (e.g. 'm', 's', 'h') are routed to
    processSerialCommand(char cmd) for mode control, sequence
    playback, testing, and help. See the 'h' command for a full list.

  • Multi-character sequence-upload commands begin with an uppercase
    letter ('U', 'S', or 'E') and are routed to
    handleSequenceCommand(char *cmd), which manages the upload
    protocol (start upload, define steps, finalise).

Both transports use identical routing logic: a 1-byte payload goes to
processSerialCommand(); anything longer goes to handleSequenceCommand().
*/

void setupWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  LOG("[WIFI] Access Point started: ");
  LOGLN(WIFI_SSID);
  LOG("[WIFI] IP Address: ");
  LOGLN_VAL(WiFi.softAPIP());

  // Start WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  wsReady = true;
  LOGLN("[SETUP] WebSocket server started on port 81");
}

void handleWiFiStatus() {
  // report client connections periodically
  if (millis() - lastWifiCheckTime > 20000) {
    lastWifiCheckTime = millis();
    LOGF("[WIFI] AP clients connected: %d\n", WiFi.softAPgetStationNum());
  }
}

// Called by the WebSocketsServer library whenever a WebSocket event occurs.
// WStype_t tells us what kind of event it is (connect, disconnect, message, etc).
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {

    case WStype_DISCONNECTED:
      LOGF("[WS] Client %u disconnected\n", num);
      break;

    case WStype_CONNECTED:
      {
        // 'payload' contains the URL path the client connected to
        LOGF("[WS] Client %u connected\n", num);
      }
      break;

    case WStype_TEXT:
      if (length > 0) {
        LOGF("[WS] Received payload from client %u (%d bytes)\n", num, (int)length);

        if (length == 1){
          // regular single-character command
          processSerialCommand((char)payload[0]);
        } else {
          // sequence command; string of characters
          handleSequenceCommand((char*)payload);
        }
      }
      break;

    default:
      break;
  }
}

void handleSequenceCommand(char *cmd){
  // These command strings should start with explicitly uppercase letters
  switch (cmd[0]){
    case 'U':
      if (uploadingSequence) {
        LOGLN("[SEQ] Upload rejected: another upload already in progress");
        break;
      }

      // Reset old upload buffer
      uploadingSequence = true;
      uploadStepCount = 0;
      uploadingSequenceId = -1;
      memset(&uploadSequenceBuffer, 0, sizeof(uploadSequenceBuffer));

      cmd++;
      processSequenceUploadCommand(cmd);
      break;

    case 'S':
      if (!uploadingSequence){
        LOGLN("[SEQ] Sequence step definition command rejected as no sequence is currently being uploaded");
      } else if (uploadStepCount >= MAX_SEQUENCE_LENGTH) {
        LOGF("[SEQ] Sequence step definition command rejected: sequence is full (max %d)\n", MAX_SEQUENCE_LENGTH);
      } else {
        cmd++;
        if (processSequenceStepCommand(uploadStepCount, cmd)) {
          uploadStepCount++;
        }
      }
      break;

    case 'E':
      if (!uploadingSequence){
        LOGLN("[SEQ] Upload complete command ignored: no upload in progress");
        break;
      } else {
        cmd++;
        processSequenceEndCommand(cmd);
      }
      break;

    default:
      LOGF("[SEQ] Unknown command: %c\n", cmd[0]);
      break;
  }
}

void processSequenceUploadCommand(char *cmd){
  int i = 0;
  while (cmd[i] != '\n' && cmd[i] != '\0') {
    // Make sure we have enough characters remaining to verify '=' and capture at least a 1-character value
    if (cmd[i+1] != '\0' && cmd[i+1] != '\n' && 
        cmd[i+2] != '\0' && cmd[i+2] != '\n') {
      
      // Only read characters at the start of the string or succeeding a space
      if ((i == 0 || cmd[i-1] == ' ') && cmd[i+1] == '=') {

        toLowercase(cmd[i]);
        switch (cmd[i]){
          // Sequence ID
          case 'i': {
            // use endPtr to track how many characters were parsed
            char *endPtr;
            int parsedId = (int)strtol(&cmd[i+2], &endPtr, 10);

            // if endPtr is not the same as the start pointer, then the value was parsed
            if (endPtr != &cmd[i+2] && parsedId >= 0) {
              uploadingSequenceId = parsedId;
              LOGF("[SEQ] Starting sequence upload (id=%d)...\n", uploadingSequenceId);
            } else {
              LOGLN("[SEQ] Sequence upload failed: invalid sequence ID");
              uploadingSequence = false;
            }

            break;
          }

          // Sequence name
          case 'n': {
            const char *nameStart = &cmd[i+2];
            int nameLen = strcspn(nameStart, " \n\r");
            
            if (nameLen >= sizeof(uploadSequenceBuffer.name)) {
                nameLen = sizeof(uploadSequenceBuffer.name) - 1;
            }

            strncpy(uploadSequenceBuffer.name, nameStart, nameLen);
            uploadSequenceBuffer.name[nameLen] = '\0';
            break;
          }

          // Number of steps
          case 's': {
            // use endPtr to track how many characters were parsed
            char *endPtr;
            int parsedSteps = (int)strtol(&cmd[i+2], &endPtr, 10);

            // if endPtr is not the same as the start pointer, then the value was parsed
            if (endPtr != &cmd[i+2] && parsedSteps > 0 && parsedSteps <= MAX_SEQUENCE_LENGTH) {
              uploadSequenceBuffer.length = parsedSteps;
            } else {
              LOGF("[SEQ] Sequence upload failed: invalid step count (max %d)\n", MAX_SEQUENCE_LENGTH);
              uploadingSequence = false;
            }

            break;
          }

          default:
            break;
        }
      }
    }
    i++;
  }
}

bool processSequenceStepCommand(uint8_t stepIndex, char *cmd){
  uint8_t keyIndex = 0;
  uint32_t color = 0;
  uint16_t duration = 0;
  
  bool valid = true;
  int i = 0;
  while (cmd[i] != '\n' && cmd[i] != '\0') {
    // Make sure we have enough characters remaining to verify '=' and capture at least a 1-character value
    if (cmd[i+1] != '\0' && cmd[i+1] != '\n' && 
        cmd[i+2] != '\0' && cmd[i+2] != '\n') {
      
      // Only read characters at the start of the string or succeeding a space
      if ((i == 0 || cmd[i-1] == ' ') && cmd[i+1] == '=') {

        toLowercase(cmd[i]);
        switch (cmd[i]){
          // Key index
          case 'k': {
            // use endPtr to track how many characters were parsed
            char *endPtr;
            int parsedKey = (int)strtol(&cmd[i+2], &endPtr, 10);

            // if endPtr is not the same as the start pointer, then the value was parsed
            if (endPtr != &cmd[i+2] && parsedKey >= 0 && parsedKey < NUM_KEYS) {
              keyIndex = parsedKey;
            } else {
              LOGF("[SEQ] Step %d: invalid key index\n", stepIndex);
              valid = false;
            }

            break;
          }

          // LED Color
          case 'c': {
            // use endptr to track how many characters were parsed
            char *endPtr;
            uint32_t parsedColor = strtoul(&cmd[i+2], &endPtr, 16);

            // if endptr is not the same as the start pointer, then the value was parsed
            if (endPtr != &cmd[i+2]) {
              color = parsedColor;
            } else {
              LOGF("[SEQ] Step %d: invalid color value\n", stepIndex);
              valid = false;
            }

            break;
          }
            
          // Note duration
          case 'd': {
            int parsedDuration = atoi(&cmd[i+2]);

            if (parsedDuration >= MIN_NOTE_DURATION && parsedDuration <= MAX_NOTE_DURATION) {
              duration = parsedDuration;
            } else {
              LOGF("[SEQ] Step %d: invalid duration\n", stepIndex);
              valid = false;
            }

            break;
          }

          default:
            break;
        }
      }
    }
    i++;
  }

  if (!valid) {
    LOGF("[SEQ] Step %d REJECTED due to invalid fields\n", stepIndex);
    return false;
  }

  LOGF("[SEQ] Uploaded step %d: key %d, color 0x%06X, duration %dms\n", stepIndex, keyIndex, color, duration);
  uploadSequenceBuffer.steps[stepIndex] = SequenceStep{keyIndex, color, duration};
  return true;
}

void processSequenceEndCommand(char *cmd){
  int endSeqId = -1;

  // Parse the i= (sequence ID) field if present
  int i = 0;
  while (cmd[i] != '\n' && cmd[i] != '\0') {
    if ((i == 0 || cmd[i-1] == ' ') && cmd[i+1] == '=') {
      toLowercase(cmd[i]);
      if (cmd[i] == 'i') {
        endSeqId = atoi(&cmd[i+2]);
      }
    }
    i++;
  }

  uploadingSequence = false;

  if (endSeqId != uploadingSequenceId){
    LOGF("[SEQ] Sequence upload failed: ID mismatch; expected %d, got %d\n", uploadingSequenceId, endSeqId);
    return;
  }

  if (uploadStepCount == 0){
    LOGF("[SEQ] Sequence upload failed: No steps provided\n");
    return;
  } else if (uploadStepCount != uploadSequenceBuffer.length) {
    LOGF("[SEQ] Upload failed: expected %d steps, received %d\n", uploadSequenceBuffer.length, uploadStepCount);
    return;
  }
      
  // If a name wasn't properly provided, set a default name
  if (strlen(uploadSequenceBuffer.name) == 0) {
    strcpy(uploadSequenceBuffer.name, "Unnamed Sequence");
  }
    
  currentSequence = uploadSequenceBuffer;
  uploadStepCount = 0;
  LOGF("[SEQ] Sequence upload complete (id=%d): %s (%d steps)\n", endSeqId, currentSequence.name, currentSequence.length);
}

void handleSerialCommands() {
  // Read all available bytes into the line buffer one at a time.
  // Serial data arrives byte-by-byte (unlike WebSocket, which delivers
  // complete payloads), so we must accumulate characters until a newline
  // signals the end of a line and a complete command is ready.

  // NOTE: Serial monitor must be set to "Newline" or "Both NL and CR" for 
  // commands to be properly processed by this function

  while (Serial.available()) {
    char c = (char)Serial.read();

    // Process accumulated characters when we reach a new line
    if (c == '\n' || c == '\r') {
      // If this line overflowed, just clear the flag and move on
      if (serialBufOverflow) {
        serialBufOverflow = false;
        memset(serialBuf, 0, SERIAL_BUF_SIZE);
        serialBufPos = 0;
        continue;
      }

      if (serialBufPos == 0) {
        // ignore empty lines / trailing CR
        continue;
      } else if (serialBufPos == 1) {
        // regular single-character command
        processSerialCommand(serialBuf[0]);
      } else {
        // sequence command; string of characters
        serialBuf[serialBufPos] = '\0';
        handleSequenceCommand(serialBuf);
      }

      // Reset buffer for next line
      memset(serialBuf, 0, SERIAL_BUF_SIZE);
      serialBufPos = 0;
      continue;
    }

    // If we're in overflow mode, discard bytes until the next newline
    if (serialBufOverflow) continue;

    // Accumulate character into buffer (guard against overflow)
    if (serialBufPos < SERIAL_BUF_SIZE - 1) {
      serialBuf[serialBufPos++] = c;
    } else {
      // Buffer full without newline — enter overflow mode
      LOGLN("[SERIAL] Input buffer overflow — line discarded");
      serialBufOverflow = true;
      memset(serialBuf, 0, SERIAL_BUF_SIZE);
      serialBufPos = 0;
    }
  }
}

void processSerialCommand(char cmd) {
  toLowercase(cmd);
  switch (cmd) {

    // ---- MODE CONTROL ----

    case 'm': // Manual mode
      LOGLN("\n[CMD] Received: Switch to MANUAL mode");
      if (currentMode == MANUAL) {
        LOGLN("\n[CMD] Already in MANUAL mode");
      } else {
        setMode(MANUAL);
      }
      break;

    case 'a': // Guided mode
      LOGLN("\n[CMD] Received: Switch to GUIDED mode");
      if (currentMode == GUIDED) {
        LOGLN("\n[CMD] Already in GUIDED mode");
      } else {
        setMode(GUIDED);
      }
      break;

    case 'f': // Teaching mode
      LOGLN("\n[CMD] Received: Switch to TEACHING mode");
      if (currentMode == TEACHING) {
        LOGLN("\n[CMD] Already in TEACHING mode");
      } else {
        setMode(TEACHING);
      }
      break;

    // ---- SEQUENCE CONTROL ----

    case 's': // Start sequence
      LOGLN("\n[CMD] Received: Start sequence");
      if (currentMode == MANUAL) {
        LOGLN("\n[CMD] Cannot start sequence in MANUAL mode");
      } else {
        startSequence();
      }
      break;

    case 'x': // Stop sequence
      LOGLN("\n[CMD] Received: Stop sequence");
      if (currentMode == MANUAL) {
        LOGLN("\n[CMD] Cannot stop sequence in MANUAL mode");
      } else if (!sequenceRunning) {
        LOGLN("\n[CMD] Sequence is not running");
      } else {
        stopSequence();
      }
      break;

    case 'l': // List current sequence
      LOGLN("\n========================================");
      LOGLN("         CURRENT SEQUENCE");
      LOGLN("========================================");
      LOGF("  Name: %s\n", currentSequence.name);
      LOGF("  Length: %d steps\n", currentSequence.length);
      for (int i = 0; i < currentSequence.length; i++) {
        LOGF("    Step %d: Key %d, Color %s, Duration %dms\n", 
             i, 
             currentSequence.steps[i].keyIndex, 
             getColorString(currentSequence.steps[i].color), 
             currentSequence.steps[i].duration);
      }
      LOGLN("========================================\n");
      break;

    // ---- TESTING ----

    case 't': // Test LEDs
      LOGLN("\n[CMD] Received: Test LEDs");
      testLEDs();
      break;

    case 'u': // Test servos
      LOGLN("\n[CMD] Received: Test servos");
      testServos();
      break;

    case 'g': // Toggle test log mode
      if (!testLogEnabled) {
        LOGLN("\n[CMD] Received: Enable test log mode");
        testLogStart();
      } else {
        LOGLN("\n[CMD] Received: Disable test log mode");
        testLogStop();
      }
      break;


    // ---- HELP ----

    case 'h': // Help
    case '?':
      LOGLN("\n========================================");
      LOGLN("         SERIAL COMMANDS");
      LOGLN("========================================");
      LOGLN("  MODE:");
      LOGLN("    m - Switch to MANUAL mode");
      LOGLN("    a - Switch to GUIDED mode");
      LOGLN("    f - Switch to TEACHING mode");
      LOGLN("  SEQUENCE:");
      LOGLN("    s - Start sequence");
      LOGLN("    x - Stop sequence");
      LOGLN("    l - View current sequence");
      LOGLN("  TESTING:");
      LOGLN("    t - Test LEDs");
      LOGLN("    u - Test servos");
      LOGLN("    g - Enter/Exit test log mode");
      LOGLN("  HELP:");
      LOGLN("    h - Show this help");
      LOGLN("========================================\n");
      break;

    case '\n':
    case '\r':
    case ' ':
      // Ignore
      break;

    default:
      LOGF("[CMD] Unknown command: '%c' (type 'h' for help)\n", cmd);
      break;
    }
}

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
      executeSequenceStep(getCurrentSequenceStep());
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
      LOGF("[SEQ] Same key %d in consecutive steps - waiting for servo release\n", previousKeyIndex);
      waitingForServoRelease = true;
      servoReleaseStartTime = millis();
      // Don't execute step yet - will be done on next loop iteration after delay
    } else {
      executeSequenceStep(getCurrentSequenceStep());
    }
  }
}

void handleGuidedMode() {
  if (keyJustPressed == getCurrentSequenceStep().keyIndex) {
    LOGF("[SEQ] Correct key %d pressed, advancing sequence.\n", keyJustPressed);

    resetKey(getCurrentSequenceStep().keyIndex);

    currentSequenceStepIndex++;
    if (currentSequenceStepIndex >= getCurrentSequence().length) {
      LOGLN("[SEQ] Sequence complete");
      stopSequence();
      return;
    }

    // Note: does not currently wait for sequence step duration before executing next step
    executeSequenceStep(getCurrentSequenceStep());
  }
}

/*
===============================
       SEQUENCE HANDLING
===============================
*/

// starts playing the sequence from the beginning
void startSequence() {
  if (sequenceRunning) {
    LOGLN("[SEQ] Sequence already running, ignoring start request");
    return;
  }
  
  if (getCurrentSequence().length <= 0) {
    LOGF("[ERROR] Invalid sequence length: %d encountered while starting sequence\n", getCurrentSequence().length);
    return;
  }

  LOGLN("\n[SEQ] ======== STARTING SEQUENCE ========");
  LOGF("[SEQ] Sequence: %s (%d steps)\n", getCurrentSequence().name, getCurrentSequence().length);

  sequenceRunning = true;
  currentSequenceStepIndex = 0;
  currentStepStartTime = millis();

  if (testLogEnabled) {
    testLogExpectedNextStepStartTime = currentStepStartTime;
    testLogLastAutoKey = -1;
    testLogAutoRepeatStreak = 0;
  }

  // immediately play the first step
  executeSequenceStep(getCurrentSequenceStep());
}

// stops the sequence and turns off all keys
void stopSequence() {
  if (!sequenceRunning) return;  // Nothing to stop
  
  LOGLN("\n[SEQ] ======== STOPPING SEQUENCE ========");
  LOGF("[SEQ] Total steps completed: %d/%d\n", currentSequenceStepIndex, getCurrentSequence().length);

  for (int i = 0; i < NUM_KEYS; i++) {
    resetKey(i);
  }
  
  if (testLogEnabled) {
    testLogExpectedNextStepStartTime = 0;
    testLogLastAutoKey = -1;
    testLogAutoRepeatStreak = 0;
  }

  sequenceRunning = false;
  waitingForServoRelease = false;
}

// plays a single step of a sequence
void executeSequenceStep(const SequenceStep &step) {
  if (!isValidKeyIndex(step.keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while executing sequence step\n", step.keyIndex);
    testLogLogError(TESTLOG_INVALID_KEY_INDEX, F("ERROR_INVALID_KEY"));
    currentStepStartTime = millis(); // Still update time to prevent infinite loop
    return;
  }

  LOGF("[SEQ] Step %d/%d: key=%d, color=%s, duration=%dms\n",
       currentSequenceStepIndex + 1, getCurrentSequence().length, 
       step.keyIndex, getColorString(step.color), step.duration);

    unsigned long stepStartCallTime = millis();

  // Compute autoplay timing error against expected time
  long autoplayTimingErrorMs = 0;
  if (testLogEnabled) {
    if (testLogExpectedNextStepStartTime == 0) {
      testLogExpectedNextStepStartTime = stepStartCallTime;
    }
    autoplayTimingErrorMs = (long)(stepStartCallTime - testLogExpectedNextStepStartTime);
  }

  unsigned long ledCmdStart = millis();
  // light up the key's LED with the specified color
  lightUpKey(step.keyIndex, step.color);
  unsigned long ledCmdLatencyMs = millis() - ledCmdStart;

  unsigned long servoCmdLatencyMs = 0;
  // if we're in teaching mode, also press the key with the servo
  if (currentMode == TEACHING) {
    LOGF("[SERVO] Auto-pressing key %d (channel %d)\n", step.keyIndex, keys[step.keyIndex].servoChannel);
    unsigned long servoCmdStart = millis();
    autoPressKey(step.keyIndex);
    servoCmdLatencyMs = millis() - servoCmdStart;
  }

  if (testLogEnabled) {
    int nextIndex = currentSequenceStepIndex + 1;
    bool nextIsSameKey = false;
    if (nextIndex >= 0 && nextIndex < getCurrentSequence().length) {
      nextIsSameKey = (getCurrentSequence().steps[nextIndex].keyIndex == step.keyIndex);
    }
    testLogLogAutoStep(step.keyIndex, autoplayTimingErrorMs, ledCmdLatencyMs, servoCmdLatencyMs, (uint16_t)step.duration, nextIsSameKey);
  }

  currentStepStartTime = millis();
}

/*
===============================
   KEYBOARD CONTROL FUNCTIONS
===============================
These handle button detection, sound playback, and LED control for the keys.
*/

// checks all buttons and plays/stops tones based on their state
void handleKeyPresses() {
  for (int i = 0; i < NUM_KEYS; i++) {
    bool buttonPressed = digitalRead(keys[i].buttonPin) == HIGH;

    if (buttonPressed && !keys[i].isPressed) {

      // apply debouncing to avoid false triggers
      if (millis() - lastKeyPressTime[i] >= DEBOUNCE_DELAY) {
        unsigned long pressDetectedMs = millis();

        keys[i].isPressed = true;
        lastKeyPressTime[i] = pressDetectedMs;
        toneStartTime[i] = pressDetectedMs;  // Track when this tone started

        LOGF("[KEY] Key %d PRESSED (pin %d, freq %dHz)\n", i, keys[i].buttonPin, keys[i].noteFreq);

        startKeyTone(i);
        unsigned long audioStartedMs = millis();
        
        keyJustPressed = i; // Register this key press event for guided mode

        testLogLogManualPress(i, pressDetectedMs, audioStartedMs);
      }

    } else if (!buttonPressed && keys[i].isPressed) {
      keys[i].isPressed = false;
      LOGF("[KEY] Key %d RELEASED\n", i);
      stopKeyTone(i);
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
  if (!isValidKeyIndex(keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while turning on LEDs\n", keyIndex);
    return;
  }

  LOGF("[LED] Key %d LED ON: color=%s\n", keyIndex, getColorString(color));

  for (int i = 0; i < LEDS_PER_KEY; i++) {
    keys[keyIndex].led->setPixelColor(i, color);
  }

  keys[keyIndex].led->show();
}

// turns off all LEDs on a key's LED strip
void lightDownKey(int keyIndex) {
  if (!isValidKeyIndex(keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while turning off LEDs\n", keyIndex);
    return;
  }

  LOGF("[LED] Key %d OFF\n", keyIndex);

  for (int i = 0; i < LEDS_PER_KEY; i++) {
    keys[keyIndex].led->setPixelColor(i, 0);
  }

  keys[keyIndex].led->show();
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
  if (!isValidKeyIndex(keyIndex)) {
    LOGF("[ERROR] Invalid keyIndex: %d encountered while resetting key\n", keyIndex);
    return;
  }

  lightDownKey(keyIndex);
  autoReleaseKey(keyIndex);
}

void safeServoSetAngle(uint8_t channel, uint16_t angle) {
  uint16_t clampedAngle = constrain(angle, SERVO_MIN_SAFE_ANGLE, SERVO_MAX_SAFE_ANGLE);
  
  if (clampedAngle != angle) {
    LOGF("[WARN] Servo angle clamped: %d -> %d (valid: %d-%d)\n", 
         angle, clampedAngle, SERVO_MIN_SAFE_ANGLE, SERVO_MAX_SAFE_ANGLE);
  }
  
  servoDriver.setAngle(channel, clampedAngle);
}

// ============ VALIDATION & TESTING FUNCTIONS ============

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
    if (keys[i].buttonPin < 0 || keys[i].buttonPin > 39) {
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
  
  // Flash all key LEDs white once, then off.
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

// ============ HELPERS ============

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

// Broadcasts a log message to ALL connected WebSocket clients.
// Uses broadcastTXT which sends to every connected client.
void wsBroadcastLog(const char* msg) {
  if (!wsReady) return;  // WebSocket not yet initialized
  webSocket.broadcastTXT(msg);
}

// ============ TEST LOGGING (CSV STREAM) ============
// Prints one CSV row per event to Serial (no RAM buffering).

void testLogPrintHeader() {
  Serial.println();
  Serial.println(F("CSV_BEGIN"));
  Serial.println(F("run_id,event_id,mode,event_type,key_index,repeat_streak,input_to_audio_ms,step_led_cmd_ms,step_servo_cmd_ms,autoplay_timing_error_ms,success,error_code"));
  Serial.println(F("CSV_DATA"));
}

void testLogStart() {
  testLogEnabled = true;
  testLogRunId++;
  testLogEventId = 0;

  testLogLastManualKey = -1;
  testLogLastManualTime = 0;
  testLogManualRepeatStreak = 0;

  testLogExpectedNextStepStartTime = 0;
  testLogLastAutoKey = -1;
  testLogAutoRepeatStreak = 0;

  LOGLN("\n[TESTLOG] Enabled. Streaming CSV to Serial. Press 'g' again to stop.");
  testLogPrintHeader();
}

void testLogStop() {
  if (!testLogEnabled) return;
  testLogEnabled = false;
  Serial.println(F("CSV_END"));
  LOGF("[TESTLOG] Disabled (run_id=%u, events=%u)\n", testLogRunId, testLogEventId);
}

void testLogLogManualPress(int keyIndex, unsigned long pressDetectedMs, unsigned long audioStartedMs) {
  if (!testLogEnabled) return;

  if (keyIndex == testLogLastManualKey && (pressDetectedMs - testLogLastManualTime) <= 1000) {
    testLogManualRepeatStreak++;
  } else {
    testLogManualRepeatStreak = 1;
  }
  testLogLastManualKey = (int8_t)keyIndex;
  testLogLastManualTime = pressDetectedMs;

  testLogEventId++;

  long latency = (long)(audioStartedMs - pressDetectedMs);

  Serial.print(testLogRunId); Serial.print(",");
  Serial.print(testLogEventId); Serial.print(",");
  Serial.print(getCurrentModeString()); Serial.print(",");
  Serial.print(F("MANUAL_PRESS")); Serial.print(",");
  Serial.print(keyIndex); Serial.print(",");
  Serial.print(testLogManualRepeatStreak); Serial.print(",");
  Serial.print(latency); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(1); Serial.print(",");
  Serial.println(TESTLOG_OK);
}

void testLogLogAutoStep(int keyIndex, long timingErrorMs, unsigned long ledCmdMs, unsigned long servoCmdMs, uint16_t stepDurationMs, bool nextIsSameKey) {
  if (!testLogEnabled) return;

  if (keyIndex == testLogLastAutoKey) testLogAutoRepeatStreak++;
  else testLogAutoRepeatStreak = 1;
  testLogLastAutoKey = (int8_t)keyIndex;

  testLogEventId++;

  Serial.print(testLogRunId); Serial.print(",");
  Serial.print(testLogEventId); Serial.print(",");
  Serial.print(getCurrentModeString()); Serial.print(",");
  Serial.print(F("AUTO_STEP")); Serial.print(",");
  Serial.print(keyIndex); Serial.print(",");
  Serial.print(testLogAutoRepeatStreak); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print((long)ledCmdMs); Serial.print(",");
  Serial.print((long)servoCmdMs); Serial.print(",");
  Serial.print((long)timingErrorMs); Serial.print(",");
  Serial.print(1); Serial.print(",");
  Serial.println(TESTLOG_OK);

  // Update expected time for next step (includes same-key release delay)
  testLogExpectedNextStepStartTime = testLogExpectedNextStepStartTime + (unsigned long)stepDurationMs;
  if (nextIsSameKey) {
    testLogExpectedNextStepStartTime = testLogExpectedNextStepStartTime + SERVO_RELEASE_DELAY;
  }
}

void testLogLogError(uint8_t errorCode, const __FlashStringHelper* eventType) {
  if (!testLogEnabled) return;

  testLogEventId++;

  Serial.print(testLogRunId); Serial.print(",");
  Serial.print(testLogEventId); Serial.print(",");
  Serial.print(getCurrentModeString()); Serial.print(",");
  Serial.print(eventType); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(0); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(0); Serial.print(",");
  Serial.println(errorCode);
}
