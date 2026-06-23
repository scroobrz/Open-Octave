/*
===============================
        COMMUNICATION
===============================
The firmware coordinates control and API commands across three distinct transports:

  1. USB Serial — Intended for hardwired debugging. Characters arrive one byte
     at a time via the USB UART. handleUsbSerialCommands() accumulates them
     into a line buffer until a newline is received, then dispatches the line.
     Log output is printed to Serial.

  2. WiFi WebSocket — The primary control link. The Master module connects to
     a remote WebSocket server as a client. Commands arrive as complete text
     payloads via handleWebSocketCommand(), requiring no line buffering. Logs
     are transmitted back to the server to maintain a "serial-over-WiFi" experience.

  3. Daisy Chain Serial — Handles physical modularity. Two hardware serial ports
     (Upstream and Downstream) continuously exchange heartbeats for dynamic role
     assignment (Master vs. Slave) and sequence coordination down the module chain.

The API processes commands through unified routing logic:

  - Single-character commands (e.g. 'g', 'x') go to processSingleCharCommand()
    for sequence playback control, hardware testing, etc.

  - Multi-character sequence commands (prefixed by 'U', 'S', 'E') go to
    handleSequenceCommand() for the sequence upload protocol.

External transports (USB, WebSocket) share this logic: 1-byte payloads go
to processSingleCharCommand(); anything longer goes to handleSequenceCommand().
*/

void handleChainCommunication(){
  handleSerialFromUpstream();
  handleSerialFromDownstream();
  checkHeartbeat();
  checkHeartbeatReply();
  sendHeartbeat();
}

void handleControllerCommunication(){
    webSocket.loop();
    handleUsbSerialCommands();
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
    LOGF("[DEBUG] Heartbeat timeout — last received %lums ago, upstream bytes available: %d\n",
         millis() - timeLastHeartbeatReceived, UpstreamSerial.available());
    promoteToMaster();
  }
}

// Modules with downstream neighbors check for downstream replies and reset count if lost
void checkHeartbeatReply() {
  if (numModulesInChain > moduleChainIndex + 1 &&
      millis() - timeLastHeartbeatReplyReceived >= HEARTBEAT_TIMEOUT) {
    numModulesInChain = moduleChainIndex + 1;  // Only count up to self
    LOGF("[CHAIN] Downstream lost — chain count reset to %d\n", numModulesInChain);
    if (isMaster) {
      if (sequenceRunning && currentSequenceMode == BROADCAST && numModulesInChain <= 1) {
        LOGLN("[CHAIN] Broadcast mode aborted due to lost downstream chain");
        stopSequence();
      }
      sendHelloToController();
      updateDefaultSequenceForChainSize();
    }
  }
}

// Handles incoming commands from the upstream serial port
void handleSerialFromUpstream(){
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
      handleCommandsFromUpstream();
    }
  }
}

void handleCommandsFromUpstream(){
  while (UpstreamSerial.available()) {
    if (UpstreamSerial.peek() == CHAIN_HEARTBEAT_BYTE) return;
    char c = (char)UpstreamSerial.read();

    // If buffer is empty, drop non-printable ASCII characters to prevent garbage desync from connection noise
    if (upstreamSerialBufPos == 0) {
      if (c < 33 || c > 126) {
        continue;
      }
    }

    // Process accumulated characters when we reach a new line
    if (c == '\n' || c == '\r') {
      // If this line overflowed, just clear the flag and move on
      if (upstreamSerialBufOverflow) {
        upstreamSerialBufOverflow = false;
        upstreamSerialBufPos = 0;
        continue;
      }

      if (upstreamSerialBufPos == 0) {
        // ignore empty lines / trailing CR
        continue;
      } else {
        upstreamSerialBuf[upstreamSerialBufPos] = '\0';
        char cmdType = upstreamSerialBuf[0];

        // Is it a single-char slave command (x, b)?
        // We ensure there are no extra parameters by checking if the next character is the null terminator or a trailing carriage return.
        if ((upstreamSerialBuf[1] == '\0' || upstreamSerialBuf[1] == '\r') && (cmdType == 'x' || cmdType == 'b')) {
          if (cmdType == 'x') {
            for (int i = 0; i < NUM_KEYS; i++) {
              resetKey(i);
            }
            
            if (sequenceRunning && currentSequenceMode == BROADCAST) {
              sequenceRunning = false;
              configureNotes();
            }

            DownstreamSerial.write("x\n", 2);
          } else if (cmdType == 'b') {
            currentSequenceMode = BROADCAST;
            sequenceRunning = true;
            configureNotes();
            DownstreamSerial.write("b\n", 2);
          }
        }
        // Is it a slave hardware-override command (t, g, r)?
        else if (cmdType == 't' || cmdType == 'g' || cmdType == 'r') {
          int targetModule, targetKey;
          char *endPtr;

          // Parse `<globalKeyIndex>.[<color>]`
          targetKey = (int)strtol(&upstreamSerialBuf[1], &endPtr, 10);
          targetModule = targetKey / NUM_KEYS;

          // If the moduleIndex matches ours, process the hardware logic locally
          if (targetModule == moduleChainIndex) {
            int localKeyIndex = targetKey % NUM_KEYS; // convert to local 0-11 space

            if (cmdType == 'r') {
              resetKey(localKeyIndex);
            } else if (*endPtr == '.') {
              uint32_t color = strtoul(endPtr + 1, NULL, 16);

              lightUpKey(localKeyIndex, color);
              if (cmdType == 't') {
                autoPressKey(localKeyIndex);
              }
            }
          } else if (targetModule > moduleChainIndex) {
            // Pass the message further down the chain if it's not for us
            DownstreamSerial.write((uint8_t *)upstreamSerialBuf, upstreamSerialBufPos);
            DownstreamSerial.write('\n');
          }
        }
        else if (cmdType == 'm' && upstreamSerialBuf[1] != '\0') {
          if (upstreamSerialBuf[1] == '0') {
            currentSynthMode = SYNTH_ADDITIVE;
          } else if (upstreamSerialBuf[1] == '1') {
            currentSynthMode = SYNTH_KARPLUS_STRONG;
          } else if (upstreamSerialBuf[1] == '2') {
            currentSynthMode = SYNTH_KS_OVERDRIVE;
          } else if (upstreamSerialBuf[1] == '3') {
            currentSynthMode = SYNTH_KS_HARPSICHORD;
          } else if (upstreamSerialBuf[1] == '4') {
            currentSynthMode = SYNTH_HAMMOND_ORGAN;
          }
          // Pass the message further down the chain
          DownstreamSerial.write((uint8_t *)upstreamSerialBuf, upstreamSerialBufPos);
          DownstreamSerial.write('\n');
        }
      }

      // Reset buffer for next line
      upstreamSerialBufPos = 0;
      continue;
    }

    // If we're in overflow mode, discard bytes until the next newline
    if (upstreamSerialBufOverflow) continue;

    // Accumulate character into buffer (guard against overflow)
    if (upstreamSerialBufPos < SERIAL_BUF_SIZE - 1) {
      upstreamSerialBuf[upstreamSerialBufPos++] = c;
    } else {
      // Buffer full without newline — enter overflow mode
      LOGLN("[SERIAL] Input buffer overflow — line discarded");
      upstreamSerialBufOverflow = true;
      upstreamSerialBufPos = 0;
    }
  }
}

void handleHeartbeatFromUpstream(uint8_t num){
  // Reject implausible chain indices — likely UART noise during cable insertion
  if (num >= MAX_MODULES) {
    LOGF("[CHAIN] Ignoring bogus upstream heartbeat: index=%d (max=%d)\n", num, MAX_MODULES - 1);
    return;
  }

  timeLastHeartbeatReceived = millis();

  numModulesInChain = num + 1;

  if (isMaster){
    demoteToSlave();
  }

  if (moduleChainIndex != num) {
    moduleChainIndex = num;
    configureNotes();
  }

  // Forward heartbeat downstream
  DownstreamSerial.write(CHAIN_HEARTBEAT_BYTE);
  DownstreamSerial.write(moduleChainIndex + 1);

  // Reply upstream
  UpstreamSerial.write(CHAIN_HEARTBEAT_BYTE);
  UpstreamSerial.write(moduleChainIndex);
}

// Handles incoming commands from the downstream serial port.
void handleSerialFromDownstream(){
  while (DownstreamSerial.available()){
    uint8_t byte = DownstreamSerial.peek();

    if (byte == CHAIN_HEARTBEAT_BYTE){
      // wait for both bytes
      if (DownstreamSerial.available() < 2) {
        return;
      }

      DownstreamSerial.read(); // consume
      handleHeartbeatFromDownstream(DownstreamSerial.read());
    } else {
      handleCommandsFromDownstream();
    }
  }
}

void handleCommandsFromDownstream(){
  while (DownstreamSerial.available()) {
    if (DownstreamSerial.peek() == CHAIN_HEARTBEAT_BYTE) return;
    char c = (char)DownstreamSerial.read();

    // If buffer is empty, drop non-printable ASCII characters to prevent garbage desync from connection noise
    if (downstreamSerialBufPos == 0) {
      if (c < 33 || c > 126) {
        continue;
      }
    }

    // Process accumulated characters when we reach a new line
    if (c == '\n' || c == '\r') {
      // If this line overflowed, just clear the flag and move on
      if (downstreamSerialBufOverflow) {
        downstreamSerialBufOverflow = false;
        downstreamSerialBufPos = 0;
        continue;
      }

      if (downstreamSerialBufPos == 0) {
        // ignore empty lines / trailing CR
        continue;
      } else {
        downstreamSerialBuf[downstreamSerialBufPos] = '\0';
        char cmdType = downstreamSerialBuf[0];

        if (cmdType == 'K' || cmdType == 'k') {
          int globalKey = atoi(&downstreamSerialBuf[1]);

          if (globalKey >= 0 && globalKey < MAX_TOTAL_KEYS) {
            bool isPressed = (cmdType == 'K'); // lowercase means key released
            globalKeyIsPressed[globalKey] = isPressed;

            if (isPressed) {
              globalKeyPressTime[globalKey] = millis();
            }

            if (isMaster) {
              if (recording) {
                if (isPressed) {
                  recordKeyPress(globalKey);
                } else {
                  recordKeyRelease(globalKey);
                }
              } else {
                // for guided mode
                evaluateWrongKeyFeedback(globalKey, isPressed);
              }
            }
          }
        }

        if (!isMaster) {
          UpstreamSerial.write((uint8_t *)downstreamSerialBuf, downstreamSerialBufPos);
          UpstreamSerial.write('\n');
        }
      }

      // Reset buffer for next line
      downstreamSerialBufPos = 0;
      continue;
    }

    // If we're in overflow mode, discard bytes until the next newline
    if (downstreamSerialBufOverflow) continue;

    // Accumulate character into buffer (guard against overflow)
    if (downstreamSerialBufPos < SERIAL_BUF_SIZE - 1) {
      downstreamSerialBuf[downstreamSerialBufPos++] = c;
    } else {
      // Buffer full without newline — enter overflow mode
      LOGLN("[SERIAL] Downstream input buffer overflow — line discarded");
      downstreamSerialBufOverflow = true;
      downstreamSerialBufPos = 0;
    }
  }
}

void handleHeartbeatFromDownstream(uint8_t num){
  // Reject implausible chain indices — likely UART noise during cable insertion
  if (num >= MAX_MODULES) {
    LOGF("[CHAIN] Ignoring bogus downstream heartbeat reply: index=%d (max=%d)\n", num, MAX_MODULES - 1);
    return;
  }

  timeLastHeartbeatReplyReceived = millis();

  // Only accept if this represents a higher count than we currently know.
  // Prevents flickering when intermediate modules reply before the tail's
  // forwarded reply arrives. The timeout in checkHeartbeatReply()
  // handles the count going DOWN (module removal).
  uint8_t reportedCount = num + 1;
  if (reportedCount > numModulesInChain) {
    numModulesInChain = reportedCount;
    if (isMaster) {
      sendHelloToController();
      updateDefaultSequenceForChainSize();
    }
  }

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

  if(Serial.available() && Serial.peek() == 'o'){
    Serial.read(); // consume
    if (on){
      powerOff();
    } else {
      powerOn();
    }
    return;
  }

  if (!on) return;

  while (Serial.available()) {
    char c = (char)Serial.read();

    // Process accumulated characters when we reach a new line
    if (c == '\n' || c == '\r') {
      // If this line overflowed, just clear the flag and move on
      if (serialBufOverflow) {
        serialBufOverflow = false;
        serialBufPos = 0;
        continue;
      }

      if (serialBufPos == 0) {
        // ignore empty lines / trailing CR
        continue;
      } else if (serialBufPos == 1) {
        // regular single-character command
        processSingleCharCommand(serialBuf[0]);
      } else if (serialBufPos == 2 && serialBuf[0] == 'm') {
        // synth mode command
        serialBuf[serialBufPos] = '\0';
        if (serialBuf[1] == '0') {
          currentSynthMode = SYNTH_ADDITIVE;
          LOGLN("[CMD] Received: Set Synth Mode -> Additive");
        } else if (serialBuf[1] == '1') {
          currentSynthMode = SYNTH_KARPLUS_STRONG;
          LOGLN("[CMD] Received: Set Synth Mode -> Karplus-Strong");
        } else if (serialBuf[1] == '2') {
          currentSynthMode = SYNTH_KS_OVERDRIVE;
          LOGLN("[CMD] Received: Set Synth Mode -> Overdrive");
        } else if (serialBuf[1] == '3') {
          currentSynthMode = SYNTH_KS_HARPSICHORD;
          LOGLN("[CMD] Received: Set Synth Mode -> Harpsichord");
        } else if (serialBuf[1] == '4') {
          currentSynthMode = SYNTH_HAMMOND_ORGAN;
          LOGLN("[CMD] Received: Set Synth Mode -> Hammond Organ");
        }
        // Broadcast down the chain
        DownstreamSerial.write((uint8_t*)serialBuf, serialBufPos);
        DownstreamSerial.write('\n');
      } else {
        // sequence command; string of characters
        serialBuf[serialBufPos] = '\0';
        handleSequenceCommand(serialBuf);
      }

      // Reset buffer for next line
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
      serialBufPos = 0;
    }
  }
}

// Called by the WebSocketsClient library whenever a WebSocket event occurs.
// WStype_t tells us what kind of event it is (connect, disconnect, message, etc).
// Notice that 'num' (client ID) is not present because we are the client.
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {

    case WStype_DISCONNECTED:
      wsReady = false;
      LOGLN("[WS] Disconnected from server");
      break;

    case WStype_CONNECTED:
      wsReady = true;
      LOGLN("[WS] Connected to server");
      sendHelloToController();
      break;

    case WStype_TEXT:
      if (length > 0) {
        LOGF("[WS] Received payload from server (%d bytes)\n", (int)length);
        handleWebSocketCommand((char*)payload, length);
      }
      break;

    default:
      break;
  }
}

void handleWebSocketCommand(char *cmd, size_t length){
  if (!on){
    // not listening to software while off
    return;
  }

  if (length == 1){
    // regular single-character command
    processSingleCharCommand(cmd[0]);
  } else if (length == 2 && cmd[0] == 'm') {
    // synth mode command
    if (cmd[1] == '0') {
      currentSynthMode = SYNTH_ADDITIVE;
      LOGLN("[CMD] Received: Set Synth Mode -> Additive");
    } else if (cmd[1] == '1') {
      currentSynthMode = SYNTH_KARPLUS_STRONG;
      LOGLN("[CMD] Received: Set Synth Mode -> Karplus-Strong");
    } else if (cmd[1] == '2') {
      currentSynthMode = SYNTH_KS_OVERDRIVE;
      LOGLN("[CMD] Received: Set Synth Mode -> Overdrive");
    } else if (cmd[1] == '3') {
      currentSynthMode = SYNTH_KS_HARPSICHORD;
      LOGLN("[CMD] Received: Set Synth Mode -> Harpsichord");
    } else if (cmd[1] == '4') {
      currentSynthMode = SYNTH_HAMMOND_ORGAN;
      LOGLN("[CMD] Received: Set Synth Mode -> Hammond Organ");
    }
    // Broadcast to downstream modules
    DownstreamSerial.write((uint8_t*)cmd, length);
    DownstreamSerial.write('\n');
  } else if (length > 1){
    // sequence command; string of characters
    handleSequenceCommand(cmd);
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

    case 'b': // Start sequence in broadcast mode
      LOGLN("\n[CMD] Received: Start BROADCAST mode");

      if (sequenceRunning) {
        LOGLN("\n[CMD] Sequence already running, ignoring start request");
        LOGLN("ERR cmd=b reason=already_running");
      } else {
        startSequence(BROADCAST);
        LOGLN("ACK cmd=b ok=1");
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
        const SequenceStep &s = currentSequence.steps[i];
        // Build dot-separated key and color list strings
        char keyStr[24];
        uint8_t pos = 0;
        for (uint8_t k = 0; k < s.numKeys && pos < sizeof(keyStr) - 4; k++) {
          if (k > 0) keyStr[pos++] = '.';
          pos += snprintf(&keyStr[pos], sizeof(keyStr) - pos, "%d", s.keys[k]);
        }
        keyStr[pos] = '\0';

        char colorStr[80];
        pos = 0;
        for (uint8_t k = 0; k < s.numKeys && pos < sizeof(colorStr) - 12; k++) {
          if (k > 0) { colorStr[pos++] = ','; colorStr[pos++] = ' '; }
          pos += snprintf(&colorStr[pos], sizeof(colorStr) - pos, "%s", getColorString(s.colors[k]));
        }
        colorStr[pos] = '\0';

        LOGF("    Step %d: Keys %s, Colors [%s], Duration %dms\n",
             i, keyStr, colorStr, s.duration);
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


    // ---- INFO ----

    case 'i': // Request info / connection state
      LOGLN("\n[CMD] Received: Request info / connection state");
      if (isMaster) {
        sendHelloToController();
      } else {
        LOGLN("BYE");
      }
      emitStatus();
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
      LOGLN("  POWER:");
      LOGLN("    o - Toggle module on/off");
      LOGLN("  INFO:");
      LOGLN("    i - Request connection state & info");
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
      } else if (recording) {
        LOGLN("[SEQ] Upload rejected: currently recording a sequence");
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
  uint8_t numKeys = 0;
  uint8_t stepKeys[MAX_KEYS_PER_STEP] = {0};
  uint8_t numColors = 0;
  uint32_t stepColors[MAX_KEYS_PER_STEP] = {0};
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
          // Key indices — supports dot-separated multi-key format: k=1.3.4
          case 'k': {
            char *ptr = &cmd[i+2];
            char *endPtr;

            while (numKeys < MAX_KEYS_PER_STEP) {
              int parsedKey = (int)strtol(ptr, &endPtr, 10);

              if (endPtr == ptr) {
                // No number parsed at all
                LOGF("[SEQ] Step %d: invalid key index\n", stepIndex);
                valid = false;
                break;
              }

              if (parsedKey < 0 || parsedKey >= MAX_TOTAL_KEYS) {
                LOGF("[SEQ] Step %d: key index %d out of range\n", stepIndex, parsedKey);
                valid = false;
                break;
              }

              stepKeys[numKeys++] = (uint8_t)parsedKey;

              // If next char is '.', skip it and parse the next key
              if (*endPtr == '.') {
                ptr = endPtr + 1;
              } else {
                break;  // End of key list (space, null, or newline)
              }
            }

            if (numKeys == 0) {
              LOGF("[SEQ] Step %d: no keys provided\n", stepIndex);
              valid = false;
            }

            break;
          }

          // LED Colors — supports dot-separated multi-color format: c=0000FF.FFFF00.FF00FF
          case 'c': {
            char *ptr = &cmd[i+2];
            char *endPtr;

            while (numColors < MAX_KEYS_PER_STEP) {
              uint32_t parsedColor = strtoul(ptr, &endPtr, 16);

              if (endPtr == ptr) {
                LOGF("[SEQ] Step %d: invalid color value\n", stepIndex);
                valid = false;
                break;
              }

              stepColors[numColors++] = parsedColor;

              // If next char is '.', skip it and parse the next color
              if (*endPtr == '.') {
                ptr = endPtr + 1;
              } else {
                break;  // End of color list (space, null, or newline)
              }
            }

            if (numColors == 0 && valid) {
              LOGF("[SEQ] Step %d: no colors provided\n", stepIndex);
              valid = false;
            }

            break;
          }

          // Note duration
          case 'd': {
            int parsedDuration = atoi(&cmd[i+2]);

            if (parsedDuration >= MIN_STEP_DURATION && parsedDuration <= MAX_STEP_DURATION) {
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

  // Build the step — if fewer colors than keys, repeat the last color
  SequenceStep step;
  step.numKeys = numKeys;
  step.duration = duration;
  for (uint8_t k = 0; k < numKeys; k++) {
    step.keys[k] = stepKeys[k];
    step.colors[k] = (k < numColors) ? stepColors[k] : stepColors[numColors - 1];
  }

  // Log with dot-separated key and color lists for readability
  char keyStr[24];
  uint8_t pos = 0;
  for (uint8_t k = 0; k < numKeys && pos < sizeof(keyStr) - 4; k++) {
    if (k > 0) keyStr[pos++] = '.';
    pos += snprintf(&keyStr[pos], sizeof(keyStr) - pos, "%d", stepKeys[k]);
  }
  keyStr[pos] = '\0';

  char colorStr[56];  // e.g. "0000FF.FFFF00.FF00FF"
  pos = 0;
  for (uint8_t k = 0; k < numColors && pos < sizeof(colorStr) - 8; k++) {
    if (k > 0) colorStr[pos++] = '.';
    pos += snprintf(&colorStr[pos], sizeof(colorStr) - pos, "%06X", stepColors[k]);
  }
  colorStr[pos] = '\0';

  LOGF("[SEQ] Uploaded step %d: keys=%s, colors=%s, duration=%dms\n", stepIndex, keyStr, colorStr, duration);
  uploadSequenceBuffer.steps[stepIndex] = step;
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

// Sends the HELLO registration message to the controller.
// Called on initial WS connect and whenever the chain length changes.
// Sends over WebSocket if available, otherwise over USB Serial.
void sendHelloToController() {
  char buf[24];
  snprintf(buf, sizeof(buf), "HELLO modules=%d", numModulesInChain);

  if (wsReady) {
    webSocket.sendTXT(buf);
  } else if (isMaster) {
    Serial.println(buf);
  }

  LOGF("[CTRL] Sent: %s\n", buf);
}

// Sends BYE over USB Serial to tell the controller this module is now a slave.
// The controller should stop sending commands to this port until a new HELLO arrives.
void sendByeToController() {
  Serial.println(F("BYE"));
  LOGLN("[CTRL] Sent: BYE (demoted to slave)");
}

void chainSendKeyCmd(HardwareSerial &serialPort, char cmd, int key) {
  char buf[8];
  uint8_t len = snprintf(buf, sizeof(buf), "%c%d\n", cmd, key);
  serialPort.write(buf, len);
}

void chainSendKeyCmdWithColor(HardwareSerial &serialPort, char cmd, int key, uint32_t color) {
  char buf[16];
  uint8_t len = snprintf(buf, sizeof(buf), "%c%d.%lX\n", cmd, key, (unsigned long)color);
  serialPort.write(buf, len);
}
