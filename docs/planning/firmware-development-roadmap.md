# Firmware Development Roadmap: Demos 2 & 3

> **Open Octave** — Modular, robotically enhanced educational keyboard system
>
> This document outlines the firmware and software development plan from Demo 1 through Demo 3.

---

## Timeline Overview

| Demo | Date | Theme | Status |
|------|------|-------|--------|
| **Demo 1** | 11 Feb 2026 | Robotic Fundamentals | ✅ Complete |
| **Demo 2** | 4 Mar 2026 | Full Octave + Operating Modes | 🔄 In Progress |
| **Demo 3** | 25 Mar 2026 | Modularity + Networking | ⏳ Upcoming |

---

## Current State (Post-Demo 1)

### What We Have ✅
- Multi-key support (3 keys with buttons, LEDs, servos)
- Three operating modes cycling via mode switch
- Sequence playback system (hardcoded sequences)
- Sound generation via button presses
- Debounced input handling
- Clean audio management (last-key-wins with fallback)
- Well-documented, commented codebase

### Current Hardware
- Arduino microcontroller
- PCA9685 servo driver (I2C, 16 channels)
- Adafruit NeoPixel LED sticks (10 LEDs each)
- Passive speaker (single tone via `tone()`)

---

## Demo 2: Full Octave + Operating Modes

**Deadline: 4 March 2026** (~4 weeks from now)

### Requirements from Project Plan

| # | Requirement | Priority |
|---|------------|----------|
| 1 | **Piano Mode**: Fully functional keyboard, user presses keys and plays music | 🔴 Critical |
| 2 | **Guided Mode**: Keys light up one at a time; wait for user to press correct key before advancing | 🔴 Critical |
| 3 | **Teaching Mode**: Auto-play with servos + LEDs demonstrating how to play | 🔴 Critical |
| 4 | **12 keys** (full octave) instead of current 3 | 🔴 Critical |
| 5 | **Fingering colors**: LED colors indicate which finger to use | 🟠 Important |
| 6 | **USB communication**: Switch modes and activate functions from computer | 🟠 Important |
| 7 | **Hardcoded songs**: One or two preset songs for Guided/Teaching modes | 🟠 Important |

### Firmware Tasks

#### 1. Expand to 12 Keys (Full Octave)
**Current:** 3 keys | **Target:** 12 keys (7 white + 5 black for one octave)

**Tasks:**
- [ ] Expand pin definitions for 12 keys
- [ ] Consider ESP32 migration (more GPIO pins, more PWM channels)
- [ ] May need second PCA9685 for additional servo channels (16 channels covers 12 keys)
- [ ] Update `NUM_KEYS` constant and key array
- [ ] Expand sequences to include more note options

**Pin/Channel Estimation:**
| Component | Per Key | Total (12 keys) | Notes |
|-----------|---------|-----------------|-------|
| Button | 1 GPIO | 12 pins | Digital input |
| LED Stick | 1 GPIO | 12 pins | NeoPixel data |
| Servo | 1 PCA channel | 12 channels | PCA9685 has 16 |
| **Total** | | 24 GPIO + 12 PCA | Arduino Uno has 20 GPIO... need ESP32 |

**Recommendation:** Migrate to ESP32 (38 GPIO pins, built-in WiFi for Demo 3)

#### 2. Implement Three Distinct Modes

Currently we have modes but they don't match the project spec exactly. Here's the mapping:

| Project Spec | Current Code | Changes Needed |
|--------------|--------------|----------------|
| Piano Mode | `MANUAL` | ✅ Works as-is |
| Guided Mode | `AUTOMATIC_LEDS` | ❌ Needs major changes |
| Teaching Mode | `FULL_AUTOMATIC` | ✅ Works as-is |

**Piano Mode (MANUAL)** — No changes needed
- User presses keys manually
- No LEDs or servo automation
- Sound plays on button press

**Guided Mode** — Major refactor needed
Current behavior: LEDs play through sequence automatically
Required behavior: 
- Light up ONE key at a time
- **Wait** until user presses the lit key
- Only then, advance to next note
- Provide visual/audio feedback for correct press

```cpp
// New state variables needed
int expectedKeyIndex = -1;      // Which key should be pressed next
bool waitingForKeyPress = false; // Are we waiting for user input?

void handleGuidedMode() {
  if (!sequenceRunning) return;
  
  if (!waitingForKeyPress) {
    // Light up the next key and wait
    SequenceStep& step = sequence[currentSequenceStep];
    lightUpKey(step.keyIndex, step.color);
    expectedKeyIndex = step.keyIndex;
    waitingForKeyPress = true;
  }
  // checkButtons() will detect the press and call advanceGuidedMode()
}

void advanceGuidedMode(int pressedKeyIndex) {
  if (pressedKeyIndex == expectedKeyIndex) {
    // Correct key pressed!
    resetKey(expectedKeyIndex);
    currentSequenceStep++;
    waitingForKeyPress = false;
    
    if (currentSequenceStep >= SEQUENCE_LENGTH) {
      stopSequence();
      // Could play a "success" tone here
    }
  } else {
    // Wrong key pressed - could give feedback
    // (optional: play error tone, flash LED, etc.)
  }
}
```

**Teaching Mode (FULL_AUTOMATIC)** — Already works
- LEDs light up in sequence
- Servos press keys in sequence
- Sound plays from button presses

#### 3. Implement Fingering Colors

LED colors should indicate which finger to use:
| Finger | Color | Hex Value |
|--------|-------|-----------|
| Thumb (1) | Red | `0xFF0000` |
| Index (2) | Orange | `0xFF8000` |
| Middle (3) | Yellow | `0xFFFF00` |
| Ring (4) | Green | `0x00FF00` |
| Pinky (5) | Blue | `0x0000FF` |

**Update SequenceStep struct:**
```cpp
struct SequenceStep {
  int keyIndex;     // which key (0-11)
  int fingerColor;  // color indicating which finger to use
  int duration;     // how long (ms) - used in Teaching Mode
};

// Example sequence for "Twinkle Twinkle" with fingering
const SequenceStep twinkleTwinkle[] = {
  {0, COLOR_THUMB, 500},   // C - thumb
  {0, COLOR_THUMB, 500},   // C - thumb
  {4, COLOR_PINKY, 500},   // G - pinky
  {4, COLOR_PINKY, 500},   // G - pinky
  // ... etc
};
```

#### 4. USB Serial Communication

**Purpose:** Allow a computer to control the keyboard via USB serial

**Commands to implement:**
| Command | Description | Example |
|---------|-------------|---------|
| `MODE <n>` | Switch mode (0=Piano, 1=Guided, 2=Teaching) | `MODE 1` |
| `PLAY` | Start sequence in current mode | `PLAY` |
| `STOP` | Stop current sequence | `STOP` |
| `SONG <id>` | Select which song to play | `SONG 0` |
| `STATUS` | Report current mode, sequence state | `STATUS` |

**Implementation:**
```cpp
void setup() {
  Serial.begin(9600);
  // ... rest of setup
}

void loop() {
  handleSerialCommands();  // Check for USB commands
  checkModeSwitch();
  checkButtons();
  // ... rest of loop
}

void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.startsWith("MODE ")) {
      int mode = command.substring(5).toInt();
      setMode((Mode)mode);
      Serial.println("OK");
    }
    else if (command == "PLAY") {
      startSequence();
      Serial.println("OK");
    }
    else if (command == "STOP") {
      stopSequence();
      Serial.println("OK");
    }
    else if (command == "STATUS") {
      Serial.print("MODE:");
      Serial.print(currentMode);
      Serial.print(" RUNNING:");
      Serial.println(sequenceRunning ? "1" : "0");
    }
  }
}
```

#### 5. Multiple Hardcoded Songs

**Store multiple sequences:**
```cpp
#define NUM_SONGS 2
#define MAX_SEQUENCE_LENGTH 20

const SequenceStep songs[NUM_SONGS][MAX_SEQUENCE_LENGTH] = {
  // Song 0: Twinkle Twinkle
  {
    {0, COLOR_THUMB, 500}, {0, COLOR_THUMB, 500}, 
    {4, COLOR_PINKY, 500}, {4, COLOR_PINKY, 500},
    // ...
  },
  // Song 1: Mary Had a Little Lamb
  {
    {2, COLOR_MIDDLE, 500}, {1, COLOR_INDEX, 500},
    {0, COLOR_THUMB, 500}, {1, COLOR_INDEX, 500},
    // ...
  }
};

const int songLengths[NUM_SONGS] = {14, 26};  // Length of each song
int currentSong = 0;
```

### Demo 2 Development Timeline

| Week | Dates | Tasks |
|------|-------|-------|
| **1** | 1-7 Feb | ESP32 migration, expand to 12 keys, test hardware |
| **2** | 8-14 Feb | Implement Guided Mode with wait-for-press logic |
| **3** | 15-21 Feb | USB serial protocol, fingering colors, multiple songs |
| **4** | 22-28 Feb | Integration testing, bug fixes, demo prep |
| **Buffer** | 1-4 Mar | Final polish and demo rehearsal |

### Demo 2 Testing Checklist

- [ ] **Piano Mode**: All 12 keys produce correct sounds when pressed
- [ ] **Guided Mode**: Keys light up one at a time; sequence only advances on correct key press
- [ ] **Teaching Mode**: Full auto-play with LEDs and servos in sync
- [ ] **Fingering**: Colors correctly indicate finger (1-5)
- [ ] **USB Control**: Can switch modes and start/stop via serial commands
- [ ] **Songs**: At least 2 hardcoded songs work in both Guided and Teaching modes

---

## Demo 3: Modularity + Networking

**Deadline: 25 March 2026** (~3 weeks after Demo 2)

### Requirements from Project Plan

| # | Requirement | Priority |
|---|------------|----------|
| 1 | **Two independent modules**: Each fully functional on its own | 🔴 Critical |
| 2 | **Physical interlocking**: Modules connect to form 2-octave keyboard | 🔴 Critical |
| 3 | **Combined functionality**: 24 keys work as one instrument | 🔴 Critical |
| 4 | **Complex songs**: More advanced songs spanning 2 octaves | 🟠 Important |
| 5 | **Software application**: Remote control via network | 🔴 Critical |
| 6 | **Remote control features**: On/off, volume, mode config | 🟠 Important |
| 7 | **Module info**: See connection status in app | 🟠 Important |
| 8 | **MIDI upload** (stretch): Upload songs as MIDI files | 🟡 Nice-to-have |
| 9 | **Student feedback** (stretch): View student performance in app | 🟡 Nice-to-have |

### Firmware Tasks

#### 1. Module Identification & Communication

Each module needs to know:
- Its unique ID
- Whether it's connected to another module
- Its position in the chain (octave 1, octave 2, etc.)

**Inter-module communication options:**
| Method | Pros | Cons |
|--------|------|------|
| I2C daisy-chain | Simple wiring | Limited distance, master/slave |
| UART serial | Point-to-point | Needs dedicated lines |
| CAN bus | Robust, multi-node | More complex |
| **WiFi mesh** | No wires, flexible | Requires ESP32, more latency |

**Recommendation:** WiFi with ESP-NOW (ESP32 peer-to-peer protocol)
- Low latency (~1-5ms)
- No router needed
- Easy to add more modules

```cpp
// Each module broadcasts its state
struct ModuleState {
  uint8_t moduleId;
  uint8_t position;       // 0 = leftmost, 1 = next, etc.
  bool isConnected;
  Mode currentMode;
  bool sequenceRunning;
};

// Master module / computer receives and coordinates
```

#### 2. WiFi Networking (ESP32)

**Connection architecture:**
```
┌─────────────────────────────────────────────────────────────┐
│                        LOCAL NETWORK                         │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│   ┌──────────────┐      WiFi       ┌──────────────────────┐ │
│   │   Teacher's  │ ◄─────────────► │  Open Octave Module  │ │
│   │  Computer    │                 │    (ESP32 #1)        │ │
│   │  (Software)  │                 └──────────────────────┘ │
│   └──────────────┘                          ▲               │
│         ▲                                   │ ESP-NOW       │
│         │                                   ▼               │
│         │                          ┌──────────────────────┐ │
│         └────────────────────────► │  Open Octave Module  │ │
│              WiFi                  │    (ESP32 #2)        │ │
│                                    └──────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

**WebSocket server on each ESP32:**
```cpp
#include <WiFi.h>
#include <WebSocketsServer.h>

WebSocketsServer webSocket = WebSocketsServer(81);

void setupWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_TEXT:
      handleCommand((char*)payload);
      break;
  }
}
```

#### 3. Module Synchronization

When modules are connected, they need to:
- Play sequences in sync (same timing)
- Know which notes each module is responsible for

**Approach: Master-slave with time sync**
```cpp
// Each module knows its octave offset
int octaveOffset = 0;  // 0 for first module, 12 for second, etc.

// When playing a sequence, each module only plays notes in its range
void executeSequenceStep(const SequenceStep& step) {
  int localKeyIndex = step.keyIndex - octaveOffset;
  
  // Only act if this note is within our octave
  if (localKeyIndex >= 0 && localKeyIndex < NUM_KEYS) {
    lightUpKey(localKeyIndex, step.fingerColor);
    if (currentMode == TEACHING) {
      autoPressKey(localKeyIndex);
    }
  }
}
```

#### 4. Software Application (Alvin & Robin)

**Stack recommendation:** 
- **Frontend**: React web app (cross-platform, no installation)
- **Backend**: Node.js or Python (WebSocket client)
- **Communication**: WebSocket to each ESP32 module

**Features to implement:**

| Feature | Description | Priority |
|---------|-------------|----------|
| Module discovery | Scan network for Open Octave devices | 🔴 Critical |
| Dashboard | Show all connected modules, their status | 🔴 Critical |
| Mode control | Switch modes for individual/all modules | 🔴 Critical |
| Song selection | Choose which hardcoded song to play | 🟠 Important |
| Volume control | Adjust speaker volume | 🟠 Important |
| MIDI upload | Upload .mid files to modules | 🟡 Stretch |
| Student tracking | Log key presses, show accuracy | 🟡 Stretch |

**Message Protocol (JSON over WebSocket):**

```json
// Software → Firmware
{"type": "SET_MODE", "mode": "GUIDED"}
{"type": "SELECT_SONG", "songId": 0}
{"type": "START_SEQUENCE"}
{"type": "STOP_SEQUENCE"}
{"type": "SET_VOLUME", "level": 80}
{"type": "GET_STATUS"}

// Firmware → Software  
{"type": "STATUS", "mode": "GUIDED", "running": true, "song": 0}
{"type": "KEY_PRESS", "key": 4, "correct": true, "timestamp": 123456}
{"type": "SEQUENCE_COMPLETE", "accuracy": 0.92}
{"type": "MODULE_INFO", "id": "octave-1", "keys": 12, "connected": true}
```

#### 5. MIDI Parsing (Stretch Goal)

**Simple MIDI-to-sequence converter:**
```cpp
// MIDI note to key index (Middle C = 60 = key 0)
int midiNoteToKeyIndex(int midiNote) {
  return midiNote - 60;  // Adjust based on octave offset
}

// Parse MIDI file and create sequence
// This could be done on the ESP32 or on the software side
```

### Demo 3 Development Timeline

| Week | Dates | Tasks |
|------|-------|-------|
| **1** | 5-11 Mar | WiFi setup, WebSocket server, inter-module communication |
| **2** | 12-18 Mar | Software app skeleton, module discovery, basic controls |
| **3** | 19-25 Mar | Multi-octave sequences, synchronization, final integration |

### Demo 3 Testing Checklist

- [ ] **Module 1**: Fully functional standalone (all Demo 2 features)
- [ ] **Module 2**: Fully functional standalone (all Demo 2 features)
- [ ] **Physical connection**: Modules interlock securely
- [ ] **Combined play**: 24-key songs play correctly across both modules
- [ ] **Synchronization**: LEDs/servos are in sync between modules
- [ ] **Software discovery**: App finds all modules on network
- [ ] **Remote control**: Can switch modes, start/stop from app
- [ ] **Status display**: App shows module connection status
- [ ] **Volume control**: Can adjust volume from app
- [ ] **MIDI upload** (stretch): Can upload and play custom songs

---

## Architecture Overview

### Current Architecture (Demo 1)

```
┌──────────────────────────────────────────┐
│              firmware_v2.cpp              │
│  ┌─────────┐ ┌─────────┐ ┌─────────────┐ │
│  │  Modes  │ │Sequences│ │   Audio     │ │
│  └────┬────┘ └────┬────┘ └──────┬──────┘ │
│       └───────────┼─────────────┘        │
│                   ▼                       │
│  ┌─────────────────────────────────────┐ │
│  │          Keys/Buttons/LEDs           │ │
│  └─────────────────────────────────────┘ │
└──────────────────────────────────────────┘
```

### Target Architecture (Demo 3)

```
┌─────────────────────────────────────────────────────────────┐
│                      SOFTWARE (React)                        │
│  ┌───────────────┐ ┌───────────────┐ ┌───────────────────┐  │
│  │   Dashboard   │ │  Song Library │ │  Student Tracker  │  │
│  └───────┬───────┘ └───────┬───────┘ └─────────┬─────────┘  │
│          └─────────────────┼───────────────────┘            │
│                            ▼                                 │
│             ┌──────────────────────────────┐                │
│             │     WebSocket Connection      │                │
│             └──────────────┬───────────────┘                │
└────────────────────────────┼────────────────────────────────┘
                             │ WiFi
┌────────────────────────────┼────────────────────────────────┐
│                            ▼                                 │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                  FIRMWARE (ESP32)                      │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │  │
│  │  │   WiFi   │ │  Modes   │ │ Sequences│ │  Audio   │  │  │
│  │  │ Handler  │ │  Manager │ │  Player  │ │  Engine  │  │  │
│  │  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘  │  │
│  │       └────────────┴────────────┴────────────┘        │  │
│  │                          ▼                             │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │              Hardware Abstraction               │  │  │
│  │  │   Keys │ Buttons │ LEDs │ Servos │ Speaker     │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────┘  │
│                            ▲                                 │
│  ┌─────────────────────────┴─────────────────────────────┐  │
│  │              ESP-NOW (Inter-module sync)               │  │
│  └─────────────────────────┬─────────────────────────────┘  │
│                            ▼                                 │
│                    [ Module 2, 3, ... ]                      │
└──────────────────────────────────────────────────────────────┘
```

---

## File Structure (Target)

```
firmware/
├── src/
│   ├── main.cpp              # setup() and loop()
│   ├── config.h              # Pin definitions, constants
│   ├── keys.h / keys.cpp     # Key struct and management
│   ├── audio.h / audio.cpp   # Sound generation (ESP32 LEDC)
│   ├── leds.h / leds.cpp     # LED control
│   ├── servos.h / servos.cpp # Servo control via PCA9685
│   ├── modes.h / modes.cpp   # Mode switching logic
│   ├── sequences.h           # Song/sequence definitions
│   ├── sequence_player.cpp   # Playback logic for all modes
│   ├── communication.h       # Protocol definitions
│   ├── serial_handler.cpp    # USB serial commands (Demo 2)
│   └── wifi_handler.cpp      # WiFi/WebSocket (Demo 3)
└── platformio.ini            # Build configuration

software/
├── frontend/                 # React web app
│   ├── src/
│   │   ├── App.jsx
│   │   ├── components/
│   │   │   ├── Dashboard.jsx
│   │   │   ├── ModuleCard.jsx
│   │   │   ├── SongSelector.jsx
│   │   │   └── ModeControl.jsx
│   │   └── services/
│   │       └── websocket.js
│   └── package.json
└── README.md
```

---

## Risk Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| ESP32 GPIO not enough | Low | High | Use GPIO expanders or multiplex |
| WiFi latency too high | Medium | Medium | Use ESP-NOW for inter-module, optimize protocol |
| 12-key hardware not ready | Medium | High | Test with fewer keys, scale up |
| Guided Mode timing issues | Medium | Medium | Add configurable tolerances |
| Software team bandwidth | Medium | Medium | Keep firmware API simple, good documentation |

---

## Questions & Decisions Needed

1. **ESP32 migration timing**: Start immediately or after Demo 2 requirements confirmed?
2. **Inter-module protocol**: WiFi mesh vs wired (I2C/UART)?
3. **Software framework**: Web app (React) or desktop app (Electron)?
4. **MIDI parsing**: On device or in software?
5. **Student tracking**: What data to collect? Privacy considerations?

---

## Team Responsibilities

| Team Member | Demo 2 Focus | Demo 3 Focus |
|-------------|--------------|--------------|
| Ahmad | Lead: Electronics, ESP32 migration | Firmware: Inter-module comms |
| Robin | Firmware: Guided Mode, USB serial | Software: WebSocket integration |
| Alvin | (Remote support) | Lead: Software application |
| Tommy | Firmware: Sequences, fingering | Firmware: WiFi networking |
| Shuoshuo | Firmware testing | Software: Frontend development |
