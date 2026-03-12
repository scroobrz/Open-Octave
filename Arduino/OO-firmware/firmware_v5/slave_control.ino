/*
===============================
   SLAVE MODULE CONTROL LOGIC
===============================
*/

void demoteToSlave(){
    LOGLN("[CHAIN] Upstream detected — demoting to SLAVE");
    isMaster = false;

    LOGLN("[SETUP] Disconnecting from WiFi...");
    disconnectWebsocket();
    disconnectWifi();
    LOGLN("[SETUP] WiFi & WebSocket disconnected");
}

void configureNotes(){
    // TODO: once laurie has figured out audio processing, use this 
    // function to automatically configure the notes for this slave 
    // module based on index (octave).
}