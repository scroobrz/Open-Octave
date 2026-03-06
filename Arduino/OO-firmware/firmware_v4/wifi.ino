/*
===============================
        WIFI & WEBSOCKET
===============================
WiFi Access Point setup, connection status reporting,
and WebSocket event handling.
*/

void setupWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  LOG("[WIFI] Access Point started: ");
  LOGLN(WIFI_SSID);
  LOG("[WIFI] IP Address: ");
  LOGLN_VAL(WiFi.softAPIP());

  // Start WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  wsReady = true;
  LOGLN("[SETUP] WebSocket server started on port 81");
}

void checkWiFiStatus() {
  // report client connections periodically
  if (millis() - lastWifiCheckTime > 20000) {
    lastWifiCheckTime = millis();
    LOGF("[WIFI] AP clients connected: %d\n", WiFi.softAPgetStationNum());
  }
}

// Called by the WebSocketsServer library whenever a WebSocket event occurs.
// WStype_t tells us what kind of event it is (connect, disconnect, message, etc).
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {

    case WStype_DISCONNECTED:
      LOGF("[WS] Client %u disconnected\n", num);
      break;

    case WStype_CONNECTED:
      {
        LOGF("[WS] Client %u connected\n", num);
      }
      break;

    case WStype_TEXT:
      if (length > 0) {
        LOGF("[WS] Received payload from client %u (%d bytes)\n", num, (int)length);
        handleWebSocketCommand((char*)payload, length);
      }
      break;

    default:
      break;
  }
}