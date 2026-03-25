/*
===============================
   SLAVE MODULE CONTROL LOGIC
===============================
*/

void demoteToSlave(){
    LOGLN("[CHAIN] Upstream detected — demoting to SLAVE");
    if (recording) stopRecording();
    stopSequence();
    isMaster = false;

    LOGLN("[SETUP] Disconnecting from WiFi...");
    disconnectWebsocket();
    disconnectWifi();
    LOGLN("[SETUP] WiFi & WebSocket disconnected");
}

const int baseNoteFreqs[NUM_KEYS] = {
    KEY0_NOTE, KEY1_NOTE, KEY2_NOTE, KEY3_NOTE, KEY4_NOTE, KEY5_NOTE,
    KEY6_NOTE, KEY7_NOTE, KEY8_NOTE, KEY9_NOTE, KEY10_NOTE, KEY11_NOTE
};

void configureNotes(){
    LOGF("[SETUP] Configuring notes for module index %d (octave shift: %d)\n", moduleChainIndex, moduleChainIndex);
    for (int i = 0; i < NUM_KEYS; i++) {
        keys[i].noteFreq = baseNoteFreqs[i] << moduleChainIndex;
    }
}