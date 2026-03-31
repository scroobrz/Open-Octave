/*
===============================
  MASTER MODULE CONTROL LOGIC
===============================
*/

void promoteToMaster(){
    LOGLN("[CHAIN] Upstream lost — promoting to MASTER");
    if (recording) stopRecording();
    stopSequence();
    isMaster = true;
    moduleChainIndex = 0;
    configureNotes();
    numModulesInChain = 1;

    // Flush serial buffers to discard any UART noise from cable disconnection
    while (UpstreamSerial.available()) UpstreamSerial.read();
    while (DownstreamSerial.available()) DownstreamSerial.read();
    upstreamSerialBufPos = 0;
    upstreamSerialBufOverflow = false;
    downstreamSerialBufPos = 0;
    downstreamSerialBufOverflow = false;

    // Reset heartbeat timers so the downstream timeout doesn't fire
    // while the chain is still stabilizing after separation
    timeLastHeartbeatSent = millis();
    timeLastHeartbeatReplyReceived = millis();

    updateDefaultSequenceForChainSize();

    // Re-announce to controller over USB serial (was previously BYE'd as slave)
    sendHelloToController();

    startControllerConnection();
}