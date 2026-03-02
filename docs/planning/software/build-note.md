# Open Octave Software (Demo 2) – Build Notes

This document explains how to set up and run the Open Octave software on a new laptop for Demo 2.

Demo 2 software goals:
- Provide a simple web UI to control the robot via the controller.
- Controller communicates to firmware primarily over USB Serial.
- Optional: WiFi WebSocket transport for testing if available.
- Maintain a software-side sequence library in SQLite.
- Upload exactly one selected sequence to firmware (firmware holds default + last uploaded).

---

## Repo structure (relevant parts)

- `controller/`
  - `server.js` (Node controller)
  - `database/sqlite.js` (SQLite DB layer for sequences)
  - `database/open-octave.sqlite3` (created at runtime if missing)
- `software/web/`
  - Vite + React UI

---

## Prerequisites (new laptop)

1. Install Node.js (minimum: Node 20.x LTS)
2. Install npm (usually comes with Node)
3. For Serial control:
   - Install the appropriate USB serial driver if required by your board (varies by board/USB chip).
   - Have a USB cable that supports data (not charge-only).

Optional tools (useful for debugging):
- `curl` (macOS usually has it)
- `sqlite3` CLI (macOS usually has it; if not, install via Homebrew)
- DB Browser for SQLite (GUI) if you prefer

---

## Install steps (first time)

From repo root:

### 1) Install controller dependencies
```bash
cd controller
npm install
```

### 2) Install web UI dependencies
```bash
cd ../software/web
npm install
```

---

## Environment settings (controller)

Controller reads env vars via `controller/.env` (dotenv).

Create `controller/.env` if missing.

Minimal recommended for Demo 2 (Serial primary):

- `APP_PORT=3000`
- `COMM_MODE=SERIAL`
- `SERIAL_PORT=/dev/cu.usbserial-XXXX` (macOS example)
- `SQLITE_PATH=` (optional, defaults to `controller/database/open-octave.sqlite3`)

Notes:
- `SERIAL_PORT` is machine-specific. Each teammate must set it to their own serial device path.
- On macOS, serial devices usually look like:
  - `/dev/cu.usbserial-XXXX`
  - `/dev/cu.usbmodemXXXX`

To find your port on macOS:
```bash
ls /dev/cu.*
```

---

## Running the system

### Terminal 1: start controller
```bash
cd controller
node server.js
```

Check it is running:
```bash
curl http://localhost:3000/api/health
```

### Terminal 2: start web UI
```bash
cd software/web
npm run dev
```

Open the Vite URL in the browser (usually `http://localhost:5173`).

---

## Using the UI (Demo 2 workflow)

### 1) Connect (Serial)
Open the UI → **Connect** tab:
- Transport: `SERIAL`
- Serial port: enter your local port path (example `/dev/cu.usbmodem1101`)
- Click **Connect**

Verify:
- **Settings** tab shows: Connected = true, Transport = SERIAL
- **Logs** tab shows incoming lines once the device prints anything

### 2) Basic control tests (safe tests)
Use **Control** tab:
- Mode buttons (manual / guided / teaching) -> sends `m`, `a`, `f`
- Start/Stop -> `s` / `x`
- Next/Prev -> `n` / `p`

Use **Settings → Quick Diagnostics**:
- `Refresh Health`
- `Refresh State`
- `Refresh Logs`
- `Status (GET /api/status)` (sends `?`)
- `Current seq (GET /api/seq/list, l)` (sends `l`)

Important note:
- `GET /api/seq/list` returns a “command sent” confirmation.
- The actual printed sequence content appears in **Logs**.

### 3) Sequences (software library + upload)
Open **Sequences** tab.

There are two sections:
1) **Software library (SQLite)**
2) **Current sequence on device (firmware 'l')** (verification via logs)

To seed presets:
- Click **Seed demos**
  - Seeds 8 preset sequences that match the firmware’s preset patterns (colors ignored; key+duration preserved).

To create/edit a sequence:
- Go **Settings → Create / Update sequence (SQLite)**
- Paste JSON and click **Save to DB**
- Then return to **Sequences** tab and select it from the dropdown.

To upload a DB sequence to firmware:
- **Sequences tab → Software library**
- Select a sequence
- Click **Upload to device**
- You should see an upload summary (sentCount, ok, etc.)
- Verify by clicking **Refresh (l)** and checking Logs output.

Firmware behavior reminder:
- Firmware stores only the default sequence + the last uploaded sequence.
- Upload overwrites any previously uploaded sequence.

---

# WiFi (WebSocket) Connection Setup – Demo 2

WiFi mode is optional for Demo 2 but fully supported. Serial remains the primary demo path. Use WiFi only if required or for testing flexibility.

---

## 1) Firmware Requirements

The firmware must:
- Start a WebSocket server (commonly on port `81`)
- Print its IP address over Serial at boot (recommended)
- Accept single-character commands over WebSocket

Default assumptions (adjust if your firmware differs):

- ESP32 IP (AP mode): `192.168.4.1`
- WebSocket Port: `81`

---

## 2) Connect Laptop to ESP32

### If ESP32 is in Access Point (AP) mode:
1. Open laptop WiFi settings.
2. Connect to the ESP32 network (example: `OpenOctave_AP`).
3. Default gateway/IP is usually `192.168.4.1`.

### If ESP32 is in Station (STA) mode:
1. Ensure laptop and ESP32 are on the same local network.
2. Find ESP32 IP:
   - Check Serial boot logs.
   - Or check router device list.

---

## 3) Controller Environment (.env)

If starting directly in WiFi mode:

```
APP_PORT=3000
COMM_MODE=WIFI
ESP32_IP=192.168.4.1
WS_PORT=81
```

Then start controller:

```bash
cd controller
node server.js
```

You should see:
```
[INIT] Starting in WIFI (WebSocket) mode targeting ...
[WS] Connecting to ws://...
```

---

## 4) Connect via UI (Recommended Method)

Instead of restarting controller, you can switch transport from the UI:

1. Go to **Connect tab**
2. Select `WIFI`
3. Enter:
   - ESP32 IP (example `192.168.4.1`)
   - WS Port (example `81`)
4. Click **Connect**

Verify:
- Settings tab shows `Transport: WIFI`
- `Connected: true`
- Logs tab shows `[ESP32] ...` lines

---

## 5) Test WiFi Connection

### Quick test from UI:
- Press `Manual` (sends `m`)
- Press `Status` (sends `?`)
- Click `Current seq (l)`

Check Logs tab for responses.

### Quick test from terminal:
```bash
curl -X POST "http://localhost:3000/api/modes?mode=manual"
```

If WebSocket is connected, response should be:
```json
{ "success": true, "mode": "websocket", "cmd": "m" }
```

---

## 6) Important WebSocket Framing Rule (Handled Automatically)

Firmware behavior:
- Single-character commands must be sent WITHOUT newline.
- Multi-line upload strings can be newline-terminated.

The controller automatically handles this:
- WebSocket single-char → no newline
- Upload lines → newline added
- Serial → always newline

No manual adjustment needed.

---

## 7) Common WiFi Debugging Steps

### WebSocket Not Connected
- Ensure laptop is on the correct WiFi network.
- Confirm IP address is correct.
- Ping the ESP32:
```bash
ping 192.168.4.1
```
- Restart controller.

### Connected but No Logs
- Check firmware is printing over WebSocket.
- Verify WebSocket port matches firmware.

### Reconnect Behavior
The controller auto-reconnects with exponential backoff:
1s → 2s → 4s → 8s → 10s (max)

If switching back to Serial, use UI Disconnect first.

---

WiFi mode is stable for Demo 2 but Serial remains the recommended primary transport for live demonstration reliability.

## Database management (SQLite)

### Where the DB is stored
Default path:
- `controller/database/open-octave.sqlite3`

If `SQLITE_PATH` is set in `.env`, the DB file will be at that path instead.

### Reset DB (clean slate)
Stop the controller first, then delete the DB file:
```bash
rm controller/database/open-octave.sqlite3
```

Restart controller, then re-seed presets from UI.

This is the recommended approach for Demo 2 if the dropdown shows old sequences.

### Delete a single sequence (CLI)
Use sqlite3:

```bash
sqlite3 controller/database/open-octave.sqlite3
```

Inside sqlite prompt:
```sql
SELECT id, name, updated_at FROM sequences ORDER BY updated_at DESC;
DELETE FROM sequences WHERE id = 'twinkle';
.quit
```

### Edit a sequence (CLI)
```bash
sqlite3 controller/database/open-octave.sqlite3
```

Example: update name:
```sql
UPDATE sequences SET name='New Name', updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id='twinkle';
.quit
```

Note:
- The software stores sequence steps as JSON in `data_json`.
- Easiest edit path is usually via the UI (Settings → Create/Update) rather than editing JSON by hand in SQL.

---

## Debugging guide (how to diagnose issues quickly)

### A) “UI shows connected but nothing happens”
1. Check **Settings → Refresh Health** and **Refresh State**
2. Check **Logs** for any outgoing “CTRL send” lines and any incoming “ESP32” lines
3. Verify serial port path is correct:
   - macOS: `ls /dev/cu.*`
4. If Serial is stuck, disconnect/reconnect from UI.
5. If needed, restart controller.

### B) “Upload works but sequence output doesn’t show”
1. Upload summary should show `ok: true` and `sentCount`
2. Click **Current seq (l)** and look at **Logs**
3. If logs don’t show printed sequence, firmware may not be printing it, or the transport isn’t receiving logs.

### C) “Prefixed only shows nothing”
Prefixed filter only shows lines that start with:
- `ACK `
- `STATUS `
- `EVT `
- `ERR `

Many firmware prints are plain logs; use Prefixed-only only when you expect protocol-prefixed lines.

### D) “Can’t connect to Serial”
- Make sure nothing else is using the serial port (Arduino IDE Serial Monitor, another script, etc.)
- Replug USB
- Check the port path changed

### E) “Web UI doesn’t show latest changes”
- Restart Vite:
```bash
cd software/web
npm run dev
```
- Hard refresh browser: Cmd + Shift + R

---

## Features included in Demo 2 software

Controller (`controller/server.js`):
- Serial + WiFi (WebSocket) transports, switchable via UI
- Command translation to single-char firmware commands
- Demo 2 framing workaround:
  - WebSocket: single-char commands send without newline, multi-line upload includes newline
  - Serial: always newline-terminated
- In-memory log buffer and `/api/logs`
- Controller state mirror `/api/state`
- SQLite sequence library endpoints:
  - list/get/create/update sequences
  - seed preset sequences (8 presets)
  - upload one sequence to firmware using generated U/S/E lines

Web UI (`software/web`):
- Connect tab: select transport and connect/disconnect
- Control tab: modes + sequence control commands
- Sequences tab:
  - software library (SQLite): seed/select/upload
  - device verification via firmware `l` + logs
- Logs tab: tail, auto, prefixed-only filter, search, copy, clear UI
- Settings tab:
  - connection mirror + reset mirror
  - quick diagnostics buttons
  - create/update sequence via JSON textarea

---

## Known intentional Demo 2 deviations (documented decisions)

- Firmware newline framing differs per transport:
  - Serial requires newline to execute commands.
  - WebSocket single-char commands are sent without newline due to firmware dispatch rules.
- SQLite schema is simplified for Demo 2:
  - steps stored as JSON (not normalized relational tables).
- Raw command sender is intentionally not enabled in UI to reduce risk to hardware during Demo 2.

---