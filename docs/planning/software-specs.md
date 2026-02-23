# Open Octave – Demo 2 Architecture & Integration Specification  
**(Software ↔ Firmware ↔ Electronics Alignment Document)**

---

# 1. Demo 2 Scope and Locked Decisions

## 1.1 Objectives

Demo 2 must deliver:

- Replacement of Demo 1 CLI with Software (Node controller + Web UI)
- Firmware-controlled Guided and Teaching logic
- USB Serial as primary communication path
- WiFi WebSocket as optional path
- Clean architecture that scales to Demo 3 (multi-module networking)
- Flexible protocol that avoids rewriting firmware later

---

## 1.2 Transport Decisions

### Primary (Demo 2)
- USB Serial @ 115200 baud

### Optional (Demo 2)
- ESP32 SoftAP
- WebSocket server on port 81
- Same single-character command format over both transports

### Future (Demo 3)
- Raspberry Pi hosts:
  - WebSocket server
  - SQLite database
  - WiFi Access Point
- ESP32 modules become WebSocket clients

---

# 2. Command & Protocol Architecture

## 2.1 Command Philosophy

- **Single-character commands** → button-like actions
- **Text-based structured responses** → machine-readable state and events
- Same command format across Serial and WebSocket

---

## 2.2 Idempotency Rules (Locked)

| Command | Behavior |
|----------|----------|
| Mode change (`m`, `a`, `f`) | Idempotent. No effect if already in that mode. |
| `s` (start) | Ignored if already running. |
| `x` (stop) | Idempotent. Stops immediately. |
| Sequence select | Stops current sequence before switching. |
| Restart | Separate command if needed (not `s`). |

Stop confirmation popup is handled in the UI (default = Yes on Enter).

---

## 2.3 Machine-Readable Responses (Required)

Firmware must emit structured messages prefixed with:

- `ACK`
- `STATUS`
- `EVT`
- `ERR`

### Examples

```
ACK mode=GUIDED
ACK start=ok seq=3
ACK stop=ok
ERR cmd=start reason=manual_mode
STATUS mode=GUIDED running=1 seq=3 step=4
EVT guided_step key=2 duration=500
EVT correct_press key=2
EVT wrong_press expected=2 got=5
EVT hold_complete key=2
EVT sequence_complete
```

Controller updates state ONLY from these structured messages.

---

## 2.4 Status Query (Locked)

New command: `q`

Firmware response: ```STATUS mode= running=<0|1> seq= step=```

