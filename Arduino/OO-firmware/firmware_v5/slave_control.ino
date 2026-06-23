/*
===============================
   SLAVE MODULE CONTROL LOGIC
===============================
*/

void demoteToSlave(){
    LOGLN("[CHAIN] Upstream detected — demoting to SLAVE");
    if (recording) stopRecording();
    stopSequence();

    // Notify controller over USB Serial before going silent
    sendByeToController();

    isMaster = false;

    // Flush serial buffers to discard any UART noise accumulated during
    // the physical cable insertion that triggered this role change.
    while (UpstreamSerial.available()) UpstreamSerial.read();
    while (DownstreamSerial.available()) DownstreamSerial.read();
    upstreamSerialBufPos = 0;
    upstreamSerialBufOverflow = false;
    downstreamSerialBufPos = 0;
    downstreamSerialBufOverflow = false;

    LOGLN("[SETUP] Disconnecting from WiFi...");
    disconnectWebsocket();
    disconnectWifi();
    LOGLN("[SETUP] WiFi & WebSocket disconnected");

    // Reset heartbeat timer to avoid immediate timeout due to disconnectWifi blocking
    timeLastHeartbeatReceived = millis();
}

const int baseNoteFreqs[NUM_KEYS] = {
    KEY0_NOTE, KEY1_NOTE, KEY2_NOTE, KEY3_NOTE, KEY4_NOTE, KEY5_NOTE,
    KEY6_NOTE, KEY7_NOTE, KEY8_NOTE, KEY9_NOTE, KEY10_NOTE, KEY11_NOTE
};

void configureNotes(){
    if (sequenceRunning && currentSequenceMode == BROADCAST) {
        currentEffectiveOctave = broadcastMasterOctave;
    } else if (isMaster) {
        // Master's effective octave IS the chain base octave
        currentEffectiveOctave = chainBaseOctave;
    } else {
        // Slave: derive from chain base + position in chain
        currentEffectiveOctave = chainBaseOctave + moduleChainIndex;
        if (currentEffectiveOctave > 7) currentEffectiveOctave = 7;
    }

    LOGF("[SETUP] Configuring notes for module index %d (effective octave: %d)\n", moduleChainIndex, currentEffectiveOctave);
    
    int8_t shift = (int8_t)currentEffectiveOctave - 4;

    for (int i = 0; i < NUM_KEYS; i++) {
        if (shift < 0) {
            keys[i].noteFreq = baseNoteFreqs[i] >> (-shift);
        } else {
            keys[i].noteFreq = baseNoteFreqs[i] << shift;
        }
    }
}