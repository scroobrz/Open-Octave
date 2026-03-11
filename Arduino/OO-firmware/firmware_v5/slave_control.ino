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