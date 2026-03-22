/*
===============================
        WIFI & WEBSOCKET
===============================
WiFi Access Point setup, connection status reporting,
and WebSocket event handling.
*/

// void connectToWifi() {
//   WiFi.config(ESP32_IP, ESP32_GATEWAY, ESP32_SUBNET);

//   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
//   LOGF("[WIFI] Connecting to %s...\n", WIFI_SSID);

//   unsigned long wifiStart = millis();
//   // Try repeatedly for 10 seconds
//   while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
//     delay(250);
//     LOG(".");
//   }
//   LOGLN("");

//   if (WiFi.status() == WL_CONNECTED) {
//     LOGF("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
//   } else {
//     LOGF("[WIFI] Connection FAILED (status: %d)\n", WiFi.status());
//   }
// }

// laptop hosting
// use DHCP instead of fixed IP config so the ESP32 can join macOS/Windows hotspots
// without assuming a fixed subnet. (fixed ip address of esp32 commented in config.h)
void connectToWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  LOGF("[WIFI] Connecting to %s...\n", WIFI_SSID);

  unsigned long wifiStart = millis();
  // try repeatedly for 10 seconds
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
    delay(250);
    LOG(".");
  }
  LOGLN("");

  if (WiFi.status() == WL_CONNECTED) {
    LOGF("[WIFI] Connected! Local IP: %s\n", WiFi.localIP().toString().c_str());
    LOGF("[WIFI] Gateway IP (host laptop): %s\n", WiFi.gatewayIP().toString().c_str());
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

    wl_status_t status = WiFi.status();
    LOGF("[WIFI] Status: %d, IP: %s\n", status, WiFi.localIP().toString().c_str());

    // laptop hosting / wifi recovery
    // If WiFi comes up after boot, start the WebSocket client then.
    if (status == WL_CONNECTED) {
      if (!isWifiConnected) {
        isWifiConnected = true;
        LOGF("[WIFI] Reconnected. Gateway IP (host laptop): %s\n", WiFi.gatewayIP().toString().c_str());
      }

      if (!wsReady) {
        LOGLN("[WS] WiFi is connected and WebSocket is not active. Starting WebSocket client...");
        connectToWebsocket();
      }
    } else {
      if (isWifiConnected) {
        LOGLN("[WIFI] Lost connection");
      }
      isWifiConnected = false;
      wsReady = false;
    }
  }
}

// void connectToWebsocket(){
//   // Connect to the controller (acting as the WebSocket server).
//   webSocket.begin(CONTROLLER_IP, CONTROLLER_PORT, "/");
//   webSocket.onEvent(webSocketEvent);
//   // Auto-reconnect every 5 seconds if connection gets dropped
//   webSocket.setReconnectInterval(2500);
//   wsReady = true;
//   LOGLN("[WS] WebSocket client started");
// }

// laptop hosting
// backend server = wifi gateway, esp32 will connect to this 
// This lets the same firmware work across different laptops and hotspot subnets.
void connectToWebsocket() {
  // safety check that wifi is connected
  if (WiFi.status() != WL_CONNECTED) {
    LOGLN("[WS] Cannot connect: WiFi is not connected");
    return;
  }

  IPAddress controllerIp = WiFi.gatewayIP();

  LOGF("[WS] Connecting to controller at %s:%d\n",
       controllerIp.toString().c_str(),
       CONTROLLER_PORT);

  webSocket.begin(controllerIp.toString().c_str(), CONTROLLER_PORT, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(2500);
  wsReady = true;
  LOGLN("[WS] WebSocket client started");
}

void disconnectWebsocket() {
  webSocket.disconnect();
  wsReady = false;
  LOGLN("[WS] Disconnected");
}

// Called by the WebSocketsClient library whenever a WebSocket event occurs.
// WStype_t tells us what kind of event it is (connect, disconnect, message, etc).
// Notice that 'num' (client ID) is not present because we are the client.
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {

    case WStype_DISCONNECTED:
      wsReady = false;
      LOGLN("[WS] Disconnected from server");
      break;

    case WStype_CONNECTED:
      wsReady = true;
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