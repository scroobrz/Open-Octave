# New Architecture & Remaining Work Plan

## What Has Changed Since Firmware V3

- The firmware has been migrated from Arduino Uno to ESP32 (FireBeetle ESP32 V4.0)
    - New pin assignments, but still need to be finalised 
- The communication architecture has been completely redesigned
    - ESP32 creates its own `Open Octave` WiFi network (password: `oop321321`) for clients to connect to and communicate with it.
    - ESP32 runs a WebSocket server (`WebSocketsServer.h` on port 81). Both WiFi and serial use the same single-character command format. All ESP32 log output is broadcast back to connected WebSocket clients, creating a "serial-over-WiFi" experience. One unified command protocol.
    - A new proxy server has been created in `controller/server.js` to route HTTP requests into firmware commands, and works over wired USB serial and WiFi WebSocket. It uses the `ws` npm package as a WebSocket client. Both WiFi and serial modes use the same fire-and-forget pattern.
- Guided Mode has been implemented, but still needs to be developed further. The main current limitation is that the LEDs just stay lit up for repeated sequence steps on the same key, when realistically they should flash once per keypress. There should also be some way to indicate how long each key should be pressed.
- Mode names were updated: `auto_leds` → `guided`, `full_auto` → `teaching`


## Current System Architecture
**See `system-design/system-design.html`**

- The ESP32 creates a WiFi Access Point called "Open Octave" (password: `oop321321`)
- The ESP32 runs a WebSocket server on port 81
- The controller server (`server.js`) connects to the ESP32's WiFi and opens a WebSocket connection to `ws://192.168.4.1:81`
- The controller (proxy server) is also able to connect to the ESP32 via USB serial and send commands in the same way we have been doing so far via the serial monitor
- The teacher's browser (future) sends HTTP requests to the controller at `http://localhost:3000`
- The controller translates HTTP API calls into single-character commands and sends them over WebSocket or USB serial (the USB connection does not require the host device to be connected to the ESP32's Open Octave WiFi network)
- The ESP32 processes and executes commands and broadcasts all log output back over WebSocket
- The controller prints ESP32 log output to its own console


## Remaining Work

### Electronics

- Support 12 keys on the ESP32
    - Currently nothing is wired into the ESP32, we need to find solutions for powering 12 keys (main issue is how to power all the buttons and LEDs, and whether we will still be able to work with Grove components)
    - Need to assign GPIO pins for 9 more button inputs and 9 more LED data pins
    - Constraint: avoid IO6–IO11 (SPI flash), IO1/IO3 (UART0), IO34–IO39 (input-only, no internal pull-up)
    - Update sequences to use all 12 keys (key indices 0–11)

- Audio solution
    - Current solution is a single passive buzzer using Arduino `tone()` — produces basic square wave beeps
    - For a real product, we need actual musical audio output (piano sounds, not buzzer beeps)
    - Options to explore:
        - DFPlayer Mini module (plays MP3 files from an SD card, triggered by serial commands)
        - I2S DAC module (ESP32 has native I2S support, can play WAV samples stored in flash or SPIFFS)
        - External amplifier + speaker for adequate classroom volume
    - This is a significant design decision and we should do thorough research and talk to gary

- Power solution
    - Currently powered by USB from development machine (5V, limited current)
    - Need to determine power budget for 12 servos + 12 LEDs + ESP32 + audio
    - Servos under load can draw 500mA+ each — 12 servos could peak at 6A
    - Options: dedicated 5V power supply, battery pack, USB-C PD
    - Consideration: ESP32 operates at 3.3V logic in contrast to arduino's 5V logic

- Quiet servo solution
    - Current servos are audibly noisy (buzzing/clicking during sequence playback)
    - Options to explore:
        - Higher-quality digital servos (less jitter = less buzz)
        - Solenoids instead of servos (binary push/pull, no continuous signal)
        - Opening up our servos and adding some grease
        - Software tuning (making each servo movement a series of incremental steps)
    - Affects mechanical team too (servo mounting dimensions may change)

- Create shopping list of the electronics components we will need and collect dimensions for the mechanics team
    - Create a complete bill of materials with quantities and costs

- Migrate from ESP32 AP mode to station mode (for Pi-hosted network, **not until after Demo 2 probably**)
    - Currently the ESP32 creates its own WiFi network and runs a WebSocket server
    - In the final product, the Pi will create the network (using `hostapd` + `dnsmasq`) and the ESP32 will connect as a client
    - Replace `WebSocketsServer` with `WebSocketsClient` (same library, different class)
    - The ESP32 connects TO the Pi's WebSocket server instead of hosting its own
    - The command protocol (single-char commands, log streaming) stays identical
    - This will allow for many ESP32s to connect to the Pi's WebSocket server
    - Modules will broadcast their availability as masters to the Pi server via UDP

- Daisy-chaining logic (inter-module UART communication, **not until after Demo 2 probably**)
    - Master/slave architecture: leftmost keyboard module detects it is the "master" if it has nothing connected to its UART RX2 pin
    - Will need to use Serial2 (RX2/TX2 pins) if we want to keep the option for server communication over USB (as that uses Serial0)
    - Master communicates with the Pi server (WiFi or USB)
    - Slave modules communicate with the master via one-way UART serial (TX→RX chain)
    - Master forwards commands from Pi to all slaves
    - Modules should function as standalone components when not connected to the chain
    - The dasiy chain should also allow power to flow through modules so that only the master needs to be powered (or maybe not if we end up using battery power?)

- PCB design **(future)**
    - Once pin assignments, audio solution, power solution, and key count are finalised
    - Design a custom PCB to replace any jumbled wiring
    - Should include: ESP32 module footprint, PCA9685, servo connectors, LED connectors, button connectors, audio module, power regulation, UART daisy-chain connectors

### Joint Software and Electronics

- Sequence upload protocol (server -> firmware)
    - Once sequences live in the database, the controller needs a way to send a full sequence to the ESP32 for playback
    - Design a simple text protocol over WebSocket
    - Firmware changes:
        - Add a RAM buffer for uploaded sequences
        - Add a parser for the protocol


### Software

- Implement proxy-side state tracking
    - Currently the controller responds to API requests with `{"success": true, "cmd": "m"}` — it confirms the command was sent but doesn't report what actually happened on the ESP32
    - The controller should maintain a local state mirror: current mode, current sequence, whether a sequence is running, connected device list
    - When the controller sends `'a'` (guided mode), it should update its local state to `mode: "guided"`
    - API responses should return this tracked state: `{"success": true, "mode": "guided"}`
    - For `GET /api/status`, respond directly from local state instead of querying the ESP32

- Expand API with more endpoints for further control
    - Sequence upload
    - On/Off
    - Volume control
    - and more...

- Database for sequences, state tracking, persistent storage
    - Replace the hardcoded sequences in `firmware_V4_sequences.h` with server-side storage
    - Probably use SQLite (lightweight, no separate server process, perfect for Pi)
    - Schema to include:
        - `sequences` table: id, name, created_at, updated_at
        - `sequence_steps` table: id, sequence_id, step_order, key_index, color, duration_ms
        - Future tables: teacher user info, class/session data
    - New API endpoints for database

- MIDI processing
    - The server should be able to parse MIDI files and convert them into sequences
    - This allows teachers to upload standard MIDI files and have them automatically converted to Open Octave sequences
    - Use a MIDI parsing library (e.g., `midi-parser-js` or `@tonejs/midi`)
    - New API endpoint: `POST /api/sequences/import` (accepts MIDI file upload)
    - Save the converted sequence to the database

- Teacher-facing frontend
    - Dashboard features:
        - Mode switching buttons (Manual, Guided, Teaching) with active state highlighting
        - Sequence selector dropdown (populated from `GET /api/sequences`)
        - Playback controls (Start, Stop, Next, Prev)
        - Status display (current mode, playing state, step progress)
        - Connected device list (from WebSocket server tracking)
        - Live log viewer (optional — controller could relay ESP32 logs to the browser via a second WebSocket)
        - On/off and volume control

- Configure Pi as WiFi Access Point and server host **(future)**
    - Currently the controller is a WebSocket client connecting to one ESP32
    - In the final product, the controller becomes a WebSocket server that multiple ESP32s connect to
    - Configure the Node.js server to start automatically on boot (`systemd` service)
    - Make the Pi host its own WiFi network so that ESP32s can connect to it. This is OS-level configuration, not code — done via config files on the Pi

- **Maybe:** Sequence or MIDI editor **(future)**
    - A web-based interface for creating and editing sequences or MIDI files to be converted to sequences
    - Add/remove steps, change key assignments, set colors and durations
    - Visual preview of the sequence (timeline or step list)
    - Save to database


### Mechanics

- Scale keyboard design to 12 keys
    - Need keys which prevent LED bleed-through

- LED design for black keys
    - Current LEDs (NeoPixels) are designed for the white key positions
    - Black keys are narrower and raised — may need:
        - Smaller LEDs or side-mounted LEDs
        - Different mounting approach

- Key-frame design
    - Design a housing for the electronics (ESP32, PCA9685, audio module, power)
    - Should be accessible for maintenance but protected from children
    - Consider: cable pass-throughs, mounting points, possibly buttons

- Daisy-chain connector design
    - Physical connectors between keyboard modules for the UART chain
    - Needs to be robust enough for classroom use (not loose wires)
    - Options: RJ45 jacks, 3.5mm jacks, USB-C, custom magnetic connectors
    - Must carry: power (if bus-powered) + UART TX/RX + possibly detection pin


### Demo 2 tasks that can start immediately

- Proxy-side state tracking
- Database design and implementation
- Shopping list and dimensions (for known components)
- 12-key pin mapping and firmware config
- Power budget analysis
- Quiet servo research


## Open Architecture Decisions

- Wired (USB serial) vs. wireless (WiFi) connection between keyboards and Pi
    - Wired 
        - pros: guaranteed low latency, easy device discovery (list serial ports), power over USB
        - cons: cable clutter, limited range, USB port count limits number of keyboards
    - Wireless 
        - pros: one Pi can control a whole classroom, cleaner setup, no physical tethering
        - cons: WiFi latency could affect timing, needs robust reconnection handling, separate power needed
    - Current recommendation: WiFi for control commands (latency-tolerant), with the option of UDP broadcast for time-critical synchronised commands across modules (and so eventually get rid of the wired option)

- Modules functioning without server
    - For standalone operation, the firmware should default to MANUAL mode on boot and allow physical buttons on the module to switch modes
    - This is a future consideration — needs physical button(s) on the module enclosure