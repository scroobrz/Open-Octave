# ESP32 Setup Guide — Firmware V4

This guide covers how to set up the ESP32 and upload Firmware V4.

### 1. Install Arduino IDE

1. Download Arduino IDE
2. Install with default settings and open it to confirm it works.
3. Go to preferences and make "Sketchbook location" point to the Arduino folder in this repo.

### 2. Add ESP32 Board Support

1. Open **File → Preferences** (macOS: **Arduino IDE → Settings**).
2. In the **"Additional boards manager URLs"** field, paste: 
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Click OK.
4. Go to **Tools → Board → Boards Manager**, search for `esp32`, and install **"esp32 by Espressif Systems"**. This may take a few minutes.

### 3. Select Board and Port

1. Plug the ESP32 into your computer via USB.
2. Go to **Tools → Board → esp32** and select **"DFRobot FireBeetle-ESP32"** (or **"ESP32 Dev Module"** if that's not listed).
3. Go to **Tools → Port** and select the port that appeared when you plugged in the board.
   - macOS: looks like `/dev/cu.usbserial-XXXXX`
   - Windows: looks like `COM3`, `COM4`, etc.
   - Linux: looks like `/dev/ttyUSB0`
4. If no port appears, you probably need a USB driver — see the [Troubleshooting](#troubleshooting) section.

### 4. Upload the Firmware

1. In Arduino IDE, open `firmware_v4.ino`.
2. Click **→ (Upload)** to flash the firmware to the ESP32.
3. Wait for "Done uploading." in the console.

### 5. Check the Serial Monitor

1. Open **Tools → Serial Monitor**.
2. Set the baud rate to **115200** (bottom-right dropdown).
3. Press the **RST** button on the ESP32 (or re-plug USB).
4. You should see the startup log, including:
   ```
   OPEN OCTAVE FIRMWARE V4 - INIT
   [SETUP] Validating hardware config... OK
   [SETUP] Validating sequence data... OK
   ...
   [WIFI] Access Point started: Open Octave
   [WIFI] IP Address: 192.168.4.1
   ...
   [SETUP] Complete! Starting in MANUAL mode
   ```
5. You can send commands to the ESP32 through the serial monitor through the text box at the top.

### 6. Connect Over WiFi for wireless commands

The ESP32 creates its own WiFi network (it doesn't join yours).

- **SSID:** `Open Octave`
- **Password:** `oop321321`

After connecting, you won't have internet — that's normal.

The ESP32 runs a WebSocket server at `ws://192.168.4.1:81`. You can send commands (the same commands as the serial monitor) using any WebSocket client:

- **Browser:** go to https://websocketking.com, connect to `ws://192.168.4.1:81`, and type commands.
- **Command line:**
  ```bash
  npm install -g wscat
  wscat -c ws://192.168.4.1:81
  ```

## Optional - Using the Proxy server

### 7. Send Commands Through the Controller Server

The `controller/` folder contains a Node.js proxy server that sits between a web app and the ESP32. It translates REST API calls into the single-character commands the firmware expects, and support both wired and wireless routing of commands.

#### Setup

1. Make sure you have Node.js 18+ installed.
2. Navigate to the controller folder and install dependencies:
   ```bash
   cd controller
   npm install
   ```
3. Create a `.env` file in `controller/` with these settings:
   ```
   APP_PORT=3000
   COMM_MODE='WIFI'
   ESP32_IP=192.168.4.1
   WS_PORT=81
   SERIAL_PORT='/dev/cu.usbserial-XXXXX'
   ```
   - Use `COMM_MODE='WIFI'` to communicate over WiFi, or `COMM_MODE='SERIAL'` for USB.
   - If using serial, update `SERIAL_PORT` to your actual port (check **Tools → Port** in Arduino IDE).
4. Start the server:
   ```bash
   npm start
   ```
   You should see:
   ```
   OPEN OCTAVE CONTROLLER
   Server running at: http://localhost:3000
   Mode: WIFI
   ```

#### Sending Commands

You can send commands to the ESP32 through the server using `curl` or any HTTP client:

```bash
# Switch mode
curl -X POST "http://localhost:3000/api/modes?mode=manual"
curl -X POST "http://localhost:3000/api/modes?mode=guided"
curl -X POST "http://localhost:3000/api/modes?mode=teaching"

# Sequence control
curl -X POST "http://localhost:3000/api/seq/control?cmd=start"
curl -X POST "http://localhost:3000/api/seq/control?cmd=stop"
curl -X POST "http://localhost:3000/api/seq/control?cmd=next"
curl -X POST "http://localhost:3000/api/seq/control?cmd=prev"

# Select a sequence by number (0-7)
curl -X POST "http://localhost:3000/api/seq/select?id=5"

# List all sequences
curl http://localhost:3000/api/seq/list

# Run hardware tests
curl -X POST "http://localhost:3000/api/test?target=leds"
curl -X POST "http://localhost:3000/api/test?target=servos"

# Check status
curl http://localhost:3000/api/status
```