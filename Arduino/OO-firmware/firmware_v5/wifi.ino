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

  // Start WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  wsReady = true;
  LOGLN("[SETUP] WebSocket server started on port 81");
}

void checkWiFiStatus() {
  // report connection status periodically
  if (millis() - lastWifiCheckTime >= WIFI_CHECK_INTERVAL) {
    lastWifiCheckTime = millis();
    LOGF("[WIFI] Status: %d, IP: %s\n", WiFi.status(), WiFi.localIP().toString().c_str());
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