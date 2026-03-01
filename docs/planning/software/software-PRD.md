# Open Octave – Software Product Requirements Document (PRD)
## Demo 2 (with Stage 2 Upload) – Final Integration Version

---

# 1. Overview

## 1.1 Purpose

This PRD defines the complete software requirements for Open Octave Demo 2.

The software must:

- Communicate reliably with `firmware_v4`
- Support USB Serial (primary) and WebSocket (optional)
- Maintain a controller-side state mirror
- Store sequences in SQLite
- Upload exactly ONE active sequence to firmware RAM
- Remain low-risk and deterministic
- Be extensible for Demo 3

---

# 2. Firmware Contract & Version Requirement

## 2.1 Purpose

Software depends on specific firmware capabilities.  
To avoid integration ambiguity, firmware must conform to this contract.

---

## 2.2 Minimum Required Firmware

Firmware must emit at startup:

```
FIRMWARE_VERSION=<version>
FIRMWARE_CONTRACT=1.0
```

If `FIRMWARE_CONTRACT=1.0` is not present, software must:

- Disable structured state tracking
- Disable upload functionality
- Fall back to limited command relay mode

---

## 2.3 Firmware Contract v1.0 Requirements

Firmware must implement:

1. Newline-delimited command parsing
2. Prefixed logs (`ACK`, `STATUS`, `EVT`, `ERR`)
3. Emit `STATUS` after every `ACK`
4. Guided mode event logs
5. Stage 2 upload protocol (`U/S/E`)
6. Single active uploaded sequence in RAM
7. WebSocket payload null-termination safety
8. FREE_HEAP logging at boot and upload begin

---

# 3. System Architecture

## 3.1 Components

- Node.js backend (controller)
- SQLite database
- Browser-based UI
- ESP32 firmware_v4

---

## 3.2 Transport

### Primary
USB Serial @ 115200 baud

### Optional
WebSocket (ESP32 SoftAP)

Both transports use identical newline-delimited protocol.

---

# 4. Communication Protocol

## 4.1 Command Framing (Locked)

All commands MUST end with newline.

Examples:

```
m\n
s\n
U i=3 n=Ode-To-Joy s=16\n
```

No newline → command invalid.

---

## 4.2 Supported Single-Character Commands (Demo 2)

| Command | Meaning |
|----------|----------|
| m | Manual mode |
| a | Guided mode |
| f | Teaching mode |
| s | Start sequence |
| x | Stop sequence |
| l | Print current sequence |
| t | Test LEDs |
| u | Test servos |
| g | Toggle CSV test log |
| h / ? | Help |

Software MUST NOT send unsupported commands.

---

# 5. Machine-Readable Log Protocol

Software parses ONLY lines starting with:

- `ACK `
- `STATUS `
- `EVT `
- `ERR `

All other logs ignored.

---

## 5.1 ACK

Example:

```
ACK cmd=m ok=1
```

After every ACK, firmware must emit STATUS.

---

## 5.2 STATUS

```
STATUS mode=GUIDED running=1 seq=2 step=5
```

Fields:

- mode: MANUAL | GUIDED | TEACHING
- running: 0 or 1
- seq: active sequence id/index
- step: current step index or -1

Controller updates state mirror from STATUS only.

---

## 5.3 EVT

Guided events:

```
EVT guided_step k=2 d=500
EVT correct_press k=2
EVT wrong_press e=2 g=5
EVT hold_complete k=2
EVT sequence_complete
```

Used for UI feedback and future analytics.

---

## 5.4 ERR

```
ERR cmd=s reason=manual_mode
```

Software must not assume state change on ERR.

---

# 6. Controller State Model

Software maintains in-memory state:

```
{
  transport: "serial" | "ws",
  connected: boolean,
  mode: "MANUAL" | "GUIDED" | "TEACHING",
  running: boolean,
  seq: number,
  step: number,
  uploading: boolean,
  uploadSeqId: number | null,
  uploadProgress: number | null,
  lastError: string | null
}
```

State is derived only from prefixed logs.

---

# 7. Database (Required for Stage 2)

SQLite required.

## 7.1 Tables

### sequences
- id INTEGER PRIMARY KEY
- name TEXT
- created_at TEXT
- updated_at TEXT

### sequence_steps
- id INTEGER PRIMARY KEY
- sequence_id INTEGER
- step_order INTEGER
- key_index INTEGER
- color INTEGER
- duration_ms INTEGER

---

## 7.2 Rules

- Database is source of truth.
- Firmware stores only one active sequence in RAM.
- Switching sequence → upload required.
- Runtime step updates are NOT stored in DB.

---

# 8. Stage 2 Upload Protocol

## 8.1 Format

Begin:

```
U i=<seqId> n=<name> s=<stepCount>
```

Step:

```
S i=<index> k=<keyIndex> c=<RRGGBB> d=<durationMs>
```

End:

```
E i=<seqId>
```

All lines newline-delimited.

---

## 8.2 Upload Rules

- One active uploaded sequence only.
- Uploaded sequence becomes active automatically.
- Oversized uploads rejected safely.
- Color format: exactly 6 hex characters.
- Name normalized (spaces → `-`).

---

## 8.3 Upload Flow

1. Send U
2. Wait for `ACK upload=begin`
3. Send S lines
4. Send E
5. Wait for `ACK upload=end`
6. Wait for `STATUS`

---

# 9. RAM Strategy

Firmware:

- Uses fixed buffer for uploaded steps.
- Rejects oversized upload.
- Logs free heap at:
  - Boot
  - Upload begin

Example log:

```
FREE_HEAP=238944
```

Software respects firmware upload rejection.

---

# 10. Error Handling

Software must handle:

- Missing ACK
- ERR responses
- Transport disconnect
- Partial upload failure
- Timeout during upload

Upload must abort cleanly.

---

# 11. Transport Handling

## 11.1 Serial

- Auto-reconnect
- Line-based parsing
- Primary demo path

## 11.2 WebSocket

- Same protocol
- Optional path
- Null-terminated payload parsing required in firmware

---

# 12. CSV Test Log Compatibility

CSV output must not use prefixed keywords.

Controller ignores non-prefixed lines.

---

# 13. Non-Goals (Demo 2)

Not required:

- Multi-device support
- Device IDs
- Heartbeat
- JSON protocol
- Persistent firmware storage
- Multiple uploaded sequences

---

# 14. Demo 3 Future Extensions (Not Required Now)

- Multi-device routing
- Device ID prefix
- Periodic heartbeat
- Cloud sync
- Sequence editor UI

---

# 15. Low-Risk Principles

- Line-based protocol only
- No JSON over firmware link
- Prefixed structured logs
- Deterministic RAM usage
- Single active uploaded sequence
- Database as source of truth
- STATUS after every ACK

---

# Final Requirement

Software generated from this PRD must:

1. Send newline-terminated commands.
2. Parse only prefixed logs.
3. Maintain local state mirror from STATUS.
4. Use SQLite for sequence storage.
5. Upload exactly one active sequence.
6. Treat Serial as primary transport.
7. Fail gracefully if firmware contract not met.