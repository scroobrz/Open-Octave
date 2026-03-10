/*
===============================
       COMMAND HANDLING
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

  - Single-character commands (e.g. 'm', 's', 'h') are routed to
    processSingleCharCommand(char cmd) for mode control, sequence
    playback, testing, and help. See the 'h' command for a full list.

  - Multi-character sequence-upload commands begin with an uppercase
    letter ('U', 'S', or 'E') and are routed to
    handleSequenceCommand(char *cmd), which manages the upload
    protocol (start upload, define steps, finalise).

Both transports use identical routing logic: a 1-byte payload goes to
processSingleCharCommand(); anything longer goes to handleSequenceCommand().
*/

void handleWebSocketCommand(char *cmd, size_t length){
  if (length == 1){
    // regular single-character command
    processSingleCharCommand(cmd[0]);
  } else {
    // sequence command; string of characters
    handleSequenceCommand(cmd);
  }
}

// Masters begin the heartbeat chain
void sendHeartbeat() {
  if (isMaster && millis() - timeLastHeartbeatSent >= HEARTBEAT_INTERVAL) {
    DownstreamSerial.write(CHAIN_HEARTBEAT_BYTE);
    DownstreamSerial.write(moduleChainIndex + 1);
    timeLastHeartbeatSent = millis();
  }
}

// Slaves check for upstream heartbeats and promote themselves if lost
void checkHeartbeat() {
  if (!isMaster && millis() - timeLastHeartbeatReceived >= HEARTBEAT_TIMEOUT){
    isMaster = true;
    moduleChainIndex = 0;                                                                                                              
    numModulesInChain = 1;                                                                                                                
    LOGLN("[CHAIN] Upstream lost — promoting to MASTER");

    LOGLN("[SETUP] Connecting to WiFi...");
    setupWiFi();
    LOGLN("[SETUP] WiFi & WebSocket Active!");
  }
}

// Handles incoming commands from the upstream serial port
void handleSerialCommandsFromUpstream(){
  while (UpstreamSerial.available()){
    uint8_t byte = UpstreamSerial.peek();

    if (byte == CHAIN_HEARTBEAT_BYTE){
      // wait for both bytes
      if (UpstreamSerial.available() < 2) {
        return;
      }

      UpstreamSerial.read(); // conusme
      handleHeartbeatFromUpstream(UpstreamSerial.read());
    } else {
      // TODO handle non-heartbeat intermodule commands
    }
  }
}

void handleHeartbeatFromUpstream(uint8_t num){
  timeLastHeartbeatReceived = millis();
  moduleChainIndex = num;
  numModulesInChain = num + 1;

  if (isMaster){
    isMaster = false;
    LOGLN("[CHAIN] Upstream detected — demoting to SLAVE");
  }

  // Forward heartbeat downstream
  DownstreamSerial.write(CHAIN_HEARTBEAT_BYTE);
  DownstreamSerial.write(moduleChainIndex + 1);

  // Reply upstream
  UpstreamSerial.write(CHAIN_HEARTBEAT_BYTE);
  UpstreamSerial.write(moduleChainIndex);
}

// Handles incoming commands from the downstream serial port.
void handleSerialCommandsFromDownstream(){
  while (DownstreamSerial.available()){
    uint8_t byte = DownstreamSerial.peek();

    if (byte == CHAIN_HEARTBEAT_BYTE){
      // wait for both bytes
      if (DownstreamSerial.available() < 2) {
        return;
      }

      DownstreamSerial.read(); // conusme
      handleHeartbeatFromDownstream(DownstreamSerial.read());
    } else {
      // TODO handle non-heartbeat intermodule commands
    }
  }
}

void handleHeartbeatFromDownstream(uint8_t num){
  numModulesInChain = num + 1;

  // slaves forward replies upstream
  if (!isMaster){
    UpstreamSerial.write(CHAIN_HEARTBEAT_BYTE);
    UpstreamSerial.write(num);
  }
}

// Handles incoming commands from the USB serial port
void handleUsbSerialCommands() {
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
        processSingleCharCommand(serialBuf[0]);
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

void processSingleCharCommand(char cmd) {
  toLowercase(cmd);
  switch (cmd) {

    // ---- SEQUENCE CONTROL ----

    case 'g': // Start sequence in guided mode
      LOGLN("\n[CMD] Received: Start sequence in GUIDED mode");

      if (sequenceRunning) {
        LOGLN("\n[CMD] Sequence already running, ignoring start request");
        LOGLN("ERR cmd=g reason=already_running");
      } else {
        startSequence(GUIDED);
        LOGLN("ACK cmd=g ok=1");
      }

      emitStatus();
      break;

    case 't': // Start sequence in teaching mode
      LOGLN("\n[CMD] Received: Start sequence in TEACHING mode");

      if (sequenceRunning) {
        LOGLN("\n[CMD] Sequence already running, ignoring start request");
        LOGLN("ERR cmd=t reason=already_running");
      } else {
        startSequence(TEACHING);
        LOGLN("ACK cmd=t ok=1");
      }

      emitStatus();
      break;

    case 'x': // Stop sequence
      LOGLN("\n[CMD] Received: Stop sequence");

      if (!sequenceRunning) {
        LOGLN("\n[CMD] Sequence is not running");
        LOGLN("ERR cmd=x reason=not_running");
      } else {
        stopSequence();
        LOGLN("ACK cmd=x ok=1");
      }

      emitStatus();
      break;

    case 'c': // Print current sequence
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

    case 'l': // Test LEDs
      LOGLN("\n[CMD] Received: Test LEDs");
      testLEDs();
      break;

    case 's': // Test servos
      LOGLN("\n[CMD] Received: Test servos");
      testServos();
      break;

    case 'q': // Toggle test log mode
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
      LOGLN("  SEQUENCE:");
      LOGLN("    g - Start sequence in GUIDED mode");
      LOGLN("    t - Start sequence in TEACHING mode");
      LOGLN("    x - Stop sequence");
      LOGLN("    c - View current sequence");
      LOGLN("  TESTING:");
      LOGLN("    l - Test LEDs");
      LOGLN("    s - Test servos");
      LOGLN("    q - Enter/Exit test log mode");
      LOGLN("  HELP:");
      LOGLN("    h/? - Show this help");
      LOGLN("========================================\n");
      break;

    case '\n':
    case '\r':
    case ' ':
      // Ignore
      break;

    default:
      LOGF("[CMD] Unknown command: '%c' (type 'h' or '?' for help)\n", cmd);
      break;
    }
}

// ============ SEQUENCE UPLOAD PROTOCOL ============

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
      memset(&uploadSequenceBuffer, 0, sizeof(uploadSequenceBuffer));
      uploadSequenceBuffer.id = -1;

      cmd++;
      if (processSequenceUploadCommand(cmd)){
        LOGF("[SEQ] Starting sequence upload (id=%d)...\n", uploadSequenceBuffer.id);
        LOGF("ACK upload=begin i=%d s=%d\n", uploadSequenceBuffer.id, uploadSequenceBuffer.length);
      }
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
        if (processSequenceEndCommand(cmd)){
          LOGF("[SEQ] Sequence upload complete (id=%d): %s (%d steps)\n", currentSequence.id, currentSequence.name, currentSequence.length);
          LOGF("ACK upload=end i=%d ok=1\n", currentSequence.id);
          emitStatus();
        }
      }
      break;

    default:
      LOGF("[SEQ] Unknown command: %c\n", cmd[0]);
      break;
  }
}

bool processSequenceUploadCommand(char *cmd){
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
          // Sequence ID
          case 'i': {
            // use endPtr to track how many characters were parsed
            char *endPtr;
            int parsedId = (int)strtol(&cmd[i+2], &endPtr, 10);

            // if endPtr is not the same as the start pointer, then the value was parsed
            if (endPtr != &cmd[i+2] && parsedId >= 0) {
              uploadSequenceBuffer.id = parsedId;
            } else {
              LOGLN("[SEQ] Sequence upload failed: invalid sequence ID");
              LOGLN("ERR upload=invalid_id");
              emitStatus();
              uploadingSequence = false;
              valid = false;
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
              LOGLN("ERR upload=invalid_step_count");
              emitStatus();
              uploadingSequence = false;
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

  return valid;
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

bool processSequenceEndCommand(char *cmd){
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

  if (endSeqId != uploadSequenceBuffer.id){
    LOGF("[SEQ] Sequence upload failed: ID mismatch; expected %d, got %d\n", uploadSequenceBuffer.id, endSeqId);
    LOGLN("ERR upload=id_mismatch");
    emitStatus();
    return false;
  }

  if (uploadStepCount == 0){
    LOGF("[SEQ] Sequence upload failed: No steps provided\n");
    LOGLN("ERR upload=no_steps");
    emitStatus();
    return false;
  } else if (uploadStepCount != uploadSequenceBuffer.length) {
    LOGF("[SEQ] Upload failed: expected %d steps, received %d\n", uploadSequenceBuffer.length, uploadStepCount);
    LOGLN("ERR upload=step_count_mismatch");
    emitStatus();
    return false;
  }

  // If a name wasn't properly provided, set a default name
  if (strlen(uploadSequenceBuffer.name) == 0) {
    strcpy(uploadSequenceBuffer.name, "Unnamed");
  }

  currentSequence = uploadSequenceBuffer;
  uploadStepCount = 0;
  return true;
}
