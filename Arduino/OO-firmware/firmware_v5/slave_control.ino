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