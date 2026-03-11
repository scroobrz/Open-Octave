/*
===============================
  MASTER MODULE CONTROL LOGIC
===============================
*/

void promoteToMaster(){
    LOGLN("[CHAIN] Upstream lost — promoting to MASTER");
    isMaster = true;
    moduleChainIndex = 0;
    numModulesInChain = 1;

    LOGLN("[SETUP] Connecting to WiFi...");
    connectToWifi();
    connectToWebsocket();
    LOGLN("[SETUP] WiFi & WebSocket Active!");
}

void handleControllerCommunication(){
    webSocket.loop();
    handleUsbSerialCommands();
}

