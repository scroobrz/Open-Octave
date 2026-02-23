# Open Octave – Demo 2 Architecture & Integration Specification  
**(Alignment Document)**

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

**refer controller/server.js

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

## 2.2 Idempotency Rules

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

- `ACK` (acknowledge)
- `STATUS`
- `EVT` (event)
- `ERR` (error)

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

**Backend will do the processing, and esp32 will just send the current event/ack going on

Reason: if using local mirror from microcontroller "sent", false positive communication may occur. we only know command left the controller, but not sure if esp32 received/accepted/actually changed mode. (more robust)

---

## 2.4 Status Query (Locked)

New command: `q`

Firmware response: ```STATUS mode= running=<0|1> seq= step=```

Demo 3 option (not implemented now):
- Periodic heartbeat `HB ...`

---

## 2.5 Sequence List (Machine-Readable)

New command: `L`

Response format: ```SEQ_LIST 0:PingPong|1:OdeToJoy|2:Lullaby|…```

---

# 3. Guided Mode Specification

Firmware fully owns Guided logic.

Software:
- Triggers mode
- Displays state
- Logs events
- Tracks wrong presses for future reports

---

## 3.1 Required Guided Behavior

For each step:

1. Emit: ```EVT guided_step key=K duration=D```
2. LED turns on.
3. If wrong key pressed: ```EVT wrong_press expected=K got=J```
4. On correct press: ```EVT correct_press key=K```
5. Enforce hold duration D: ```EVT hold_complete key=K```
6. Advance step.
7. On completion: ```EVT sequence_complete```

---

## 3.2 Known Limitations (To Improve)

### Repeated Notes Issue
If the same key appears consecutively:
- LED does not visually indicate new step.

Possible solutions:
- LED pulse at step start
- Require release edge before next press
- Brief LED off-on reset

### Hold Visualization
LED remains ON during hold.
Optional:
- Blink or color change when release allowed.

---

# 4. Sequence Management Plan (Two Stages)

---

# Stage 1 – Hardcoded Sequences (Demo 2 Initial)

- Sequences remain in firmware (`firmware_V4_sequences.h`)
- Controller only selects and triggers playback
- No upload protocol required
- SQLite already implemented on server for future compatibility

---

# Stage 2 – Upload Protocol + SQLite (Demo 2 Extension)

## 4.1 Sequence Upload Protocol (Locked: Option A)

Line-based text protocol using newline delimiters.

for now, choose midi files that use ONLY one octave keys to show users can upload midi files and converted into light sequences.

### Begin Upload
```U seq_id=12 name=“OdeToJoy” steps=7```

### Step Lines
```S i=0 k=1 c=00FF00 d=400
S i=1 k=1 c=00FF00 d=400
```

### End Upload
```E seq_id=12```

### Firmware Responses
ACK upload=begin seq_id=12 steps=7
ACK upload=step i=0
…
ACK upload=end seq_id=12 stored=ram
ERR upload=bad_step i=3 reason=invalid_key

---

## 4.2 Expected Upload Time

Serial throughput ≈ 11,520 bytes/sec.

Estimated upload times:

| Steps | Approx Time |
|--------|-------------|
| 16     | ~0.05 sec |
| 64     | ~0.18 sec |
| 128    | ~0.34 sec |
| 256    | ~0.68 sec |

Even with ACK responses, upload remains sub-second for normal sequence sizes.

---

## 4.3 Option B (Future Consideration Only)

Length-prefixed JSON frames.

Pros:
- Self-describing
- Structured

Cons:
- Heavier parser
- Higher firmware risk
- More memory usage

Not selected for Demo 2.

---

# 5. Server-Side Storage Decision (Locked)

We will use **SQLite starting in Demo 2 Stage 2**.

Reason:
- Simple single-file database
- Works on laptop and Raspberry Pi
- Avoids migration later
- Durable and reliable
- Industry-standard lightweight storage

---

## 5.1 SQLite Schema

### Table: sequences
- id INTEGER PRIMARY KEY
- name TEXT
- created_at TEXT
- updated_at TEXT

### Table: sequence_steps
- id INTEGER PRIMARY KEY
- sequence_id INTEGER
- step_order INTEGER
- key_index INTEGER
- color INTEGER
- duration_ms INTEGER

---

## 5.2 State Tracking Storage

- Live device state: In-memory only
- Persist sequences in SQLite
- Optionally persist:
  - Last selected sequence
  - Last known mode

Do NOT write state changes (like every step) to DB.

---

# 6. Proxy-Side State Tracking (Locked)

Old behavior:
- Controller returned `{success:true, cmd:"m"}`

New behavior:
- Controller waits for `ACK`
- Maintains local mirror:
  - mode
  - running
  - current sequence
  - step index

API returns mirror state, not just “cmd sent”.

Reason:
- Transport success ≠ firmware success
- Needed for multi-device future
- Prevents UI desync

---

# 7. MIDI Strategy

## Demo 2
- Pre-convert MIDI offline
- Hardcode sequences

## Demo 3 Direction
- Parse MIDI on server (Node.js)
- Convert to sequence steps
- Store in SQLite
- Upload using Stage 2 protocol

Library candidate:
- `@tonejs/midi`

Still required:
- Pitch mapping rules
- Tempo conversion
- Quantization
- Polyphony handling strategy

---

# 8. Firmware Responsibilities (After Review)

Firmware must add:

- ACK responses for all state-changing commands
- STATUS command (`q`)
- Machine-readable sequence list (`L`)
- Guided mode event emission
- Upload parser (Stage 2)
- Fixed-size RAM buffer for uploaded sequences
- Step validation and limits

Avoid:
- Heavy dynamic memory
- JSON parsing in firmware

---

# 9. Electronics Alignment Required

## 9.1 12-Key Scaling
- Final GPIO mapping
- LED wiring strategy (individual pins vs chained strip)
- I2C reserved pins
- Confirm pull-up strategy

## 9.2 Power
- Servo power budget
- Grounding scheme
- 3.3V logic compatibility

## 9.3 Audio
- Confirm final audio approach before implementing volume control

---

---

# 10. Additional Architectural Considerations & Constraints

This section consolidates important considerations, risks, and alignment notes that were agreed during design discussions but were not yet explicitly written into this document.

---

# 10.1 Logging, Memory Model & Safe Upload Limits 

The firmware uses structured log prefixes (`ACK`, `STATUS`, `EVT`, `ERR`) to support robust software-side state tracking.

## Logging Memory Model

- Logging macros use fixed-size stack buffers (`LOG_BUFFER_SIZE = 128`).
- No persistent log buffering is implemented.
- Logs are streamed directly to Serial and WebSocket.
- Machine-readable prefixes do **not** significantly increase RAM usage.
- Avoid heavy dynamic `String` usage inside frequent execution paths.
- No JSON parsing will be implemented in firmware.

This keeps memory deterministic and avoids heap fragmentation.

---

## Upload Memory Strategy (Safe Maximum Based on `SequenceStep` Size)

Uploaded sequences will be stored in RAM only.

No dynamic heap allocation will be used for upload storage.

Instead, firmware will use a fixed-size static buffer: ```SequenceStep uploadedSteps[MAX_UPLOADED_STEPS];```

### SequenceStep Memory Size (proposed)

```cpp
struct SequenceStep {
uint8_t keyIndex;   // 1 byte
uint32_t color;     // 4 bytes
uint16_t duration;  // 2 bytes
};
```

Estimated raw size ≈ 7 bytes  
Aligned size (typical 32-bit alignment) ≈ 8 bytes per step.

### Safe Maximum Step Count

The safe maximum will be calculated based on:

- Available free heap after firmware initialisation
- Worst-case stack usage
- Logging buffer usage
- WiFi + WebSocket memory overhead

The buffer size will be selected conservatively to:

- Avoid memory exhaustion
- Avoid heap fragmentation
- Maintain stable servo + LED timing

Example estimation (for reference only):

| Steps | Approx RAM Usage |
|--------|------------------|
| 128    | ~1 KB |
| 256    | ~2 KB |
| 512    | ~4 KB |

Final `MAX_UPLOADED_STEPS` will be locked after measuring actual free heap on ESP32 during runtime.

---

## Firmware Constraints for Electronics Team

Electronics team should expect:

- Upload storage is RAM-only (not flash).
- Maximum uploaded sequence length is fixed and enforced.
- Oversized uploads will return: ```ERR upload=too_many_steps max=```

- No dynamic heap expansion for upload.
- Deterministic memory usage is prioritised over flexibility.

This ensures:
- Stable real-time performance.
- No unexpected servo timing jitter.
- No runtime crashes due to heap fragmentation.

---

## Design Principle 

Firmware memory usage must remain:

- Deterministic
- Bounded
- Real-time safe

Flexibility (e.g., unlimited upload length) is intentionally sacrificed to maintain system stability.

---

# 10.2 Command Design Rule (Locked Principle)

To prevent protocol drift in Demo 3:

- **Single-character commands** are reserved for button-like control actions.
- **Text-based messages** are reserved for structured data transfer (upload, state, events).

---

# 10.3 Stop Confirmation – Explicit Responsibility Split

- Firmware stops immediately upon receiving `x`. (separate command from current `stop sequence`)
- Confirmation logic is handled entirely in the UI.
- UI displays confirmation modal.
- Pressing Enter defaults to "Yes". (prevent double clicking)

Firmware might not implement delayed stop logic.

---

# 10.4 Guided Mode – Open Discussion Flags

The following items require joint firmware + UX + electronics alignment before final locking:

## Repeated Notes Handling

Problem:
- Consecutive identical key steps do not visually indicate a new required press.

Options:
- LED pulse at each new step.
- Require release edge detection before accepting next press.
- Brief LED off-on reset between steps.

---

## Hold Duration Visualization

Current:
- LED remains ON during hold.

Possible improvements:
- LED blinking while hold is active.
- LED color change when release allowed.
- Progress-style visual indicator.

Requires UX discussion.

---

# 10.5 Sequence Upload – Memory Limits & ACK Strategy

## Upload Storage Decision

- Uploaded sequences are stored in RAM only.
- Not persisted in flash.
- Maximum step limit must be defined (e.g., MAX_UPLOADED_STEPS).
- Upload exceeding limit must return: ```ERR upload=too_many_steps max=```

## ACK Strategy (Low-Risk Compromise)

To balance reliability and traffic:

- `ACK upload=begin`
- Optional periodic ACK (every N steps)
- `ACK upload=end`

Per-step ACK is allowed but increases traffic.

Serial throughput remains sub-second for normal sizes.

---

# 10.6 Database Design Assumptions

SQLite is selected starting from Demo 2 Stage 2.

## Assumptions

- Controller is the single writer.
- No concurrent external DB writers.
- Sequences are low-frequency writes.
- Runtime state (step changes) is NOT written to DB.

## Risk Control

- Use transactions when inserting sequences + steps.
- Keep live state mirror in memory.
- Persist only low-frequency configuration data.

---

# 10.7 MIDI Parsing Responsibility (Explicit Separation)

Firmware will NOT parse MIDI.

All MIDI processing will occur on the server side:

1. Parse MIDI file.
2. Convert to internal step structure.
3. Apply:
 - Pitch mapping rules.
 - Tempo conversion.
 - Quantization.
 - Polyphony strategy.
4. Store in SQLite.
5. Upload to firmware via Stage 2 protocol.

This keeps firmware lightweight and real-time safe.

---

# 10.8 Demo 3 Networking – High-Level Alignment Notes

Future architecture direction:

- One Raspberry Pi controls multiple ESP32 modules.
- Each module must have a device identifier.
- Controller must track state per device.
- Commands must be routed per module.
- Upload protocol must support device targeting.

Implication:
Protocol design today must remain extensible for device addressing.

---

# 11. Upload Protocol – Future Option B (Documented for Consideration)

Alternative approach:
Length-prefixed JSON frame.

Pros:
- Self-describing.
- Easier for server-side logic.

Cons:
- Larger memory footprint.
- More parsing complexity.
- Higher firmware risk.

Not selected for Demo 2.

---

# 11.11 Architectural Risk Summary

| Risk | Mitigation |
|------|------------|
| UI state desync | ACK/STATUS-based mirror |
| Upload overflow | Fixed-size RAM buffer |
| Serial corruption | Line-delimited protocol |
| Firmware memory pressure | Avoid JSON + dynamic allocation |
| Demo 3 rewrite risk | Single-char + text separation rule |
| Multi-module scaling | Device-aware protocol planning |
s



