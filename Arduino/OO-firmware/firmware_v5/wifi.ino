/*
===============================
        WIFI & WEBSOCKET
===============================
WiFi Access Point setup, connection status reporting,
and WebSocket event handling.
*/

void connectToWifi() {
  WiFi.config(ESP32_IP, ESP32_GATEWAY, ESP32_SUBNET);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  LOGF("[WIFI] Connecting to %s...\n", WIFI_SSID);

  unsigned long wifiStart = millis();
  // Try repeatedly for 10 seconds
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
    delay(250);
    LOG(".");
  }
  LOGLN("");

  if (WiFi.status() == WL_CONNECTED) {
    LOGF("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    LOGF("[WIFI] Connection FAILED (status: %d)\n", WiFi.status());
  }
}

void disconnectWifi() {
  WiFi.disconnect();
  LOGLN("[WIFI] Disconnected");
}

void checkWifiStatus() {
  // report connection status periodically
  if (millis() - lastWifiCheckTime >= WIFI_CHECK_INTERVAL) {
    lastWifiCheckTime = millis();
    LOGF("[WIFI] Status: %d, IP: %s\n", WiFi.status(), WiFi.localIP().toString().c_str());
  }
}

void connectToWebsocket(){
  // Connect to the controller (acting as the WebSocket server).
  webSocket.begin(CONTROLLER_IP, CONTROLLER_PORT, "/");
  webSocket.onEvent(webSocketEvent);
  // Auto-reconnect every 5 seconds if connection gets dropped
  webSocket.setReconnectInterval(2500);
  wsReady = true;
  LOGLN("[WS] WebSocket client started");
}

void disconnectWebsocket() {
  webSocket.disconnect();
  wsReady = false;
  LOGLN("[WS] Disconnected");
}

// Sends the HELLO registration message to the controller.
// Called on initial WS connect and whenever the chain length changes.
void sendHelloToController() {
  if (!wsReady) return;

  char buf[24];
  snprintf(buf, sizeof(buf), "HELLO modules=%d", numModulesInChain);
  webSocket.sendTXT(buf);
  LOGF("[WS] Sent: %s\n", buf);
}

// Called by the WebSocketsClient library whenever a WebSocket event occurs.
// WStype_t tells us what kind of event it is (connect, disconnect, message, etc).
// Notice that 'num' (client ID) is not present because we are the client.
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {

    case WStype_DISCONNECTED:
      LOGLN("[WS] Disconnected from server");
      break;

    case WStype_CONNECTED:
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