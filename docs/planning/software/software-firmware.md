# Firmware Implementation Requirements – Demo 2 Alignment

## Summary

To ensure zero communication conflict between firmware and software for Demo 2 (including Stage 2 sequence upload + database integration), firmware must:

1. Keep existing debug logs.
2. Add machine-readable prefixed logs (`ACK`, `STATUS`, `EVT`, `ERR`).
3. Emit `STATUS` after every successful `ACK`.
4. Enforce newline-delimited command parsing.
5. Harden WebSocket payload parsing.
6. Implement a Stage 2 upload state machine with a single active RAM sequence buffer.
7. Automatically make uploaded sequence the active sequence (Option A).
8. Add free-heap logging to support safe RAM-based upload limits.

This approach prioritizes low risk, deterministic memory use, and simple controller parsing.

---

# 1. Logging Architecture (Low-Risk, Parallel to Existing Logs)

## 1.1 Keep Existing Logs

Do NOT remove existing logs (e.g. `[CMD]`, `[SEQ]`, `[KEY]` etc).  
They remain for debugging.

## 1.2 Add Machine-Readable Prefixed Lines

Firmware must emit additional lines starting with:

- `ACK `
- `STATUS `
- `EVT `
- `ERR `

The controller will ignore all other logs.

---

# 2. Required Prefixed Messages

## 2.1 Acknowledgements (ACK)

Emit `ACK` when a command is accepted and applied.

Examples:

```
ACK cmd=m ok=1
ACK cmd=a ok=1
ACK cmd=f ok=1
ACK cmd=s ok=1
ACK cmd=x ok=1
```

If command rejected:

```
ERR cmd=s reason=manual_mode
ERR cmd=s reason=already_running
ERR cmd=x reason=not_running
```

## 2.2 STATUS (Required After Every ACK)

After every successful `ACK`, firmware must emit:

```
STATUS mode=<MODE> running=<0|1> seq=<ID> step=<INDEX>
```

Fields:

- `mode` = MANUAL | GUIDED | TEACHING
- `running` = 0 or 1
- `seq` = current active sequence id/index
- `step` = current step index (or -1 if not running)

This prevents UI desynchronisation.

---

# 3. Guided Mode Events (Required)

Firmware must emit the following `EVT` messages:

### When new guided step becomes active:
```
EVT guided_step k=<keyIndex> d=<durationMs>
```

### On correct key press:
```
EVT correct_press k=<keyIndex>
```

### On wrong key press:
```
EVT wrong_press e=<expectedKey> g=<gotKey>
```

### When hold duration satisfied (if enforced):
```
EVT hold_complete k=<keyIndex>
```

### When sequence completes:
```
EVT sequence_complete
```

---

# 4. Transport Requirements (Locked)

## 4.1 Newline-Terminated Commands

All incoming commands (Serial and WebSocket) must be newline-delimited.

Examples:
- `m\n`
- `s\n`
- Upload lines must also end in `\n`.

Firmware already processes lines — this is now a locked requirement.

---

## 4.2 WebSocket Payload Safety (Important)

Current implementation directly passes payload pointer to parser.

Firmware must:

1. Copy payload into local buffer of size `length + 1`
2. Set `buf[length] = '\0'`
3. Pass `buf` to command-line parser

This prevents memory over-read and ensures upload reliability.

---

# 5. Sequence Upload (Top Priority for Demo 2 Extension)

## 5.1 Upload Protocol Format

Single-letter field keys:

Begin upload:
```
U i=<seqId> n=<name> s=<stepCount>
```

Step line:
```
S i=<stepIndex> k=<keyIndex> c=<RRGGBB> d=<durationMs>
```

End upload:
```
E i=<seqId>
```

All lines newline-terminated.

Color format: exactly 6 hex characters (RRGGBB).

Name handling: spaces replaced with `-` on software side.

---

## 5.2 Upload State Machine

Firmware must implement:

- `uploading = true/false`
- `uploadExpectedSteps`
- `uploadReceivedSteps`
- `uploadSeqId`
- fixed RAM buffer for uploaded steps

---

## 5.3 Active Sequence Rule 

After successful `E` line:

- Uploaded sequence automatically becomes the active sequence.
- `s` starts this uploaded sequence.
- No additional selection command required.

---

## 5.4 Upload ACK / ERR Requirements

On begin:
```
ACK upload=begin i=<seqId> s=<steps>
```

Optional periodic progress:
```
ACK upload=progress r=<received>
```

On completion:
```
ACK upload=end i=<seqId> ok=1
```

On error:
```
ERR upload=bad_line reason=<...>
ERR upload=too_many_steps max=<MAX>
```

After final `ACK`, emit `STATUS`.

---

# 6. RAM Strategy for Uploaded Sequence

Firmware stores only ONE uploaded sequence in RAM.

No flash persistence required.

Firmware must:

- Define fixed maximum step limit.
- Reject upload if exceeding limit.
- Never allow buffer overflow.

---

## 6.1 Free Heap Logging (Required)

Firmware must log free heap at:

- Boot
- Upload begin

Example:

```
LOGF("FREE_HEAP=%u\n", ESP.getFreeHeap());
```

This is required to justify safe upload limits.

No dynamic heap allocation inside upload parser.

---

# 7. Playback Engine Update

Playback must support:

- Hardcoded sequences (Stage 1)
- Uploaded active sequence (Stage 2)

Active source is whichever was last uploaded or default hardcoded at boot.

---

# 8. CSV Test Logging Compatibility

CSV test logs must not use prefixed keywords (`ACK`, `STATUS`, `EVT`, `ERR`).

Controller ignores all non-prefixed lines.

---

# 9. Some Notes for Demo 3 implementation

The following are NOT required now:

- `q` status query command
- next/previous sequence commands
- multi-device addressing
- device id fields
- heartbeat messages
- persistent storage in firmware
- multiple uploaded sequences

---

# Final Goal

After these changes:

- Software parses only prefixed lines.
- All commands are newline-delimited.
- Upload supports one active sequence in RAM.
- Controller state mirror remains reliable.
- Demo 3 can be added without rewriting protocol.
