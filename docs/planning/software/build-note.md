# Open Octave – Software Build Notes (Demo 2)

This document explains how to run and test the controller + UI on a new device,
including real ESP32 hardware.

---

# 1. Required Environment

## Node + NPM

Recommended:
- Node >= 18
- npm >= 9

Check:

```bash
node -v
npm -v
```

If not installed:
https://nodejs.org/

---

# 2. Project Structure

You must run:

- Controller: `controller/server.js`
- UI: `software/web` (Vite React app)

They are separate processes.

---

# 3. Install Dependencies (First Time Setup)

From project root:

```bash
cd controller
npm install

cd ../software/web
npm install
```

---

# 4. Running Without Hardware (Mock Mode)

This is for development or testing on a laptop without ESP32.

### Start mock ESP32 server:

```bash
cd controller
npm run mock:esp32
```

### Start controller:

```bash
node server.js
```

### Start UI:

```bash
cd ../software/web
npm run dev
```

In UI → Connect tab:

- Transport: WIFI
- IP: 127.0.0.1
- Port: 8081
- Click Connect

You should see:
- wifi.connected: true
- `[MOCK-ESP32] hello` in logs

---

# 5. Running With Real ESP32 (WiFi)

## Step 1 – Connect to ESP32 WiFi

ESP32 runs as Access Point (AP mode).

- Connect laptop WiFi to ESP32 AP network.

## Step 2 – Use Correct WebSocket Target

In UI → Connect:

- Transport: WIFI
- IP: 192.168.4.1
- Port: 81

Click Connect.

If connection fails:
- Ensure laptop is connected to ESP32 WiFi
- Ensure firmware_v4 is flashed
- Ensure no firewall blocking WebSocket

---

# 6. Running With Real ESP32 (USB Serial)

## Step 1 – Plug in ESP32

Use USB cable.

## Step 2 – Find Serial Port

macOS:
```bash
ls /dev/cu.*
```

Windows:
- Device Manager → Ports (COM & LPT)

## Step 3 – Connect

In UI → Connect:

- Transport: SERIAL
- Enter full path (example):
  `/dev/cu.usbmodem14101`
- Click Connect

If connection fails:
- Ensure Arduino Serial Monitor is CLOSED
- Ensure no other app is using port
- Try unplugging + replugging

---

# 7. IMPORTANT – Demo 2 Framing Workaround

For Demo 2, newline framing differs by transport.

WebSocket:
- Single-character commands → sent as exactly 1 byte
- NO newline appended
- Required because firmware routes by message length

Serial:
- All commands → newline-terminated
- Required because firmware dispatches on newline

This is intentional and temporary.

Do NOT modify this unless firmware newline handling is unified.

---

# 8. Switching Between Mock and Real Hardware

When moving from mock → real ESP32:

Change in UI:

Mock:
- WIFI
- 127.0.0.1
- 8081

Real ESP32:
- WIFI
- 192.168.4.1
- 81

When using USB:
- Switch to SERIAL
- Provide correct port path

No server restart required.

---

# 9. Controller Runtime Transport Switching

The controller supports:

POST /api/connect  
POST /api/disconnect  

You can switch transport without restarting Node.

However:
- Only one transport can be active at a time.
- Switching closes the other transport automatically.

---

# 10. If Commands Do Not Work

Checklist:

1. Check /api/health
   - mode correct?
   - wifi.connected true?
   - serial.open true?

2. Check Logs tab
   - Are ESP32 logs appearing?
   - Are CTRL logs showing send commands?

3. If WiFi:
   - Ensure connected to ESP32 AP
   - Ensure correct IP/port

4. If Serial:
   - Ensure correct port path
   - Ensure no other app using port

5. Restart order (safe reset):
   - Stop UI
   - Stop controller
   - Replug ESP32
   - Start controller
   - Start UI

---

# 11. Common Problems on New Devices

## macOS Serial Permission Issue

If port cannot open:
- Unplug + replug device
- Ensure correct path
- Try restarting Node

## Firewall Blocking WebSocket

If WiFi cannot connect:
- Disable firewall temporarily
- Ensure port 81 allowed

---

# 12. Demo 2 Pre-Demo Checklist

Before presentation:

- Test WiFi mode
- Test Serial mode
- Test switching between them
- Test reconnect after disconnect
- Confirm logs visible
- Confirm mode changes reflect in UI

---

# 13. Future TODO (After Demo 2)

- Unify newline framing across both transports in firmware
- Implement controller state mirror (/api/state)
- Implement structured ACK/STATUS parsing
- Add sequence upload protocol
- Add SQLite persistence