/*
===============================
        WIFI & WEBSOCKET
===============================
WiFi Access Point setup, connection status reporting,
and WebSocket event handling.
*/

// laptop hosting
// use DHCP instead of fixed IP config so the ESP32 can join macOS/Windows hotspots
// without assuming a fixed subnet. (fixed ip address of esp32 commented in config.h)
void connectToWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  isWifiConnected = false;
  wsClientStarted = false;
  wsReady = false;
  isConnectingWifi = true;
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
    isWifiConnected = true;
    isConnectingWifi = false;
  } else {
    LOGF("[WIFI] Connection FAILED (status: %d)\n", WiFi.status());
    isWifiConnected = false;
    wsClientStarted = false;
    wsReady = false;
    isConnectingWifi = false;
  }
}

// Called every loop() iteration while isMaster.
// Once WiFi connects, automatically starts the WebSocket.
void handleWifiConnection() {
  if (!isConnectingWifi) return;

  if (WiFi.status() == WL_CONNECTED) {
    LOGF("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    LOGF("[WIFI] Gateway IP (host laptop): %s\n", WiFi.gatewayIP().toString().c_str());
    isWifiConnected = true;
    isConnectingWifi = false;
    connectToWebsocket();
    LOGLN("[SETUP] WiFi & WebSocket Active!");
  }
}

void disconnectWifi() {
  WiFi.disconnect();
  isWifiConnected = false;
  isConnectingWifi = false;
  wsClientStarted = false;
  wsReady = false;
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

      if (!wsClientStarted) {
        LOGLN("[WS] WiFi is connected and WebSocket client is not started. Starting WebSocket client...");
        connectToWebsocket();
      }
    } else {
      if (isWifiConnected) {
        LOGLN("[WIFI] Lost connection");
      }
      isWifiConnected = false;
      wsClientStarted = false;
      wsReady = false;
    }
  }
}

// laptop hosting
// backend server = wifi gateway, esp32 will connect to this 
// This lets the same firmware work across different laptops and hotspot subnets.
void connectToWebsocket() {
  // safety check that wifi is connected
  if (WiFi.status() != WL_CONNECTED) {
    LOGLN("[WS] Cannot connect: WiFi is not connected");
    wsClientStarted = false;
    wsReady = false;
    return;
  }

  if (wsClientStarted) {
    LOGLN("[WS] WebSocket client already started");
    return;
  }

  IPAddress controllerIp = WiFi.gatewayIP();

  LOGF("[WS] Connecting to controller at %s:%d\n",
       controllerIp.toString().c_str(),
       CONTROLLER_PORT);

  webSocket.begin(controllerIp.toString().c_str(), CONTROLLER_PORT, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(2500);
  wsClientStarted = true;
  wsReady = false;
  LOGLN("[WS] WebSocket client started");
}

void disconnectWebsocket() {
  webSocket.disconnect();
  wsClientStarted = false;
  wsReady = false;
  LOGLN("[WS] Disconnected");
}