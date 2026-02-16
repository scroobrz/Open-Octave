---
trigger: always_on
---

# Open Octave Agent Guidelines

> Ensure consistent, safe, and effective development for the Open Octave robotically enhanced educational keyboard system.

## Project Context & Knowledge Base

- **Project Overview**: `.agent/rules/Open-Octave.md` — Core project details and milestones. Use as a core source of truth for the Open Octave project.
- **NotebookLM**: (https://notebooklm.google.com/notebook/614b20a2-c1a8-454a-8f66-34bf908c75dd) — Technical specs, datasheets, course materials. Consult using the NotebookLM MCP server before complex design decisions (protocol selection, hardware migration, new libraries), or when unsure about hardware specs or broader project requirements.

## Firmware Development Standards

### File Structure
Example firmware file structure (firmware V4 used as example):
```
Arduino/
|── OO-firmware/firmware_v4/
|    └── firmware_v4.ino        # Main logic
└── libraries/
    ├── firmware_v4_config/firmware_v4_config.h           # Hardware config, constants
    |── firmware_v4_sequences/firmware_v4_sequences.h     # Sequence definitions
    └── firmware_v4_debug/firmware_v4_debug.h             # Debug macros
```

### Clang Errors
The firmware code is written for the Arduino IDE. This can cause "false errors" within the Antigravity IDE due to missing Arduino-specific dependencies, the user is aware of this and you do not need to inform them when finding these false errors.

### Code Modification Protocols
Rule 1: **CRITICAL** Follow `git-standards.md`:
- **MUST**: Properly utilize git for version control based on the instructions in `git-standards.md`

Rule 2: Surgical Edits Only:
- **MUST**: Only modify code directly related to request
- **MUST NOT**: Refactor, reformat, or rename unrelated code

Rule 3: Respect Existing Patterns
- Follow existing structure, naming, and documentation style
- Use same conventions (e.g., `#define` vs `const int`)

### Educational Explanations
When integrating new concepts, techniques, tools, or libraries into the code, you should always provide an accompanying beginner-friendly explanation to help the user understand it and how you used it.

### Memory & Performance Optimization
Arduino Uno Constraints:
- **Flash**: 32 KB (constants, code)
- **SRAM**: 2 KB **CRITICAL** — Minimize usage!

Always Do:
- Use `F()` or `PROGMEM` for string literals
- Use smallest data types possible (`uint8_t` vs `int`)
- Pass structs by reference (`const SequenceStep &step`)

Never Do:
- Dynamic allocation (`new`, `malloc`) — causes fragmentation
- Arduino `String` class — use `char[]`
- Block `loop()` with `delay()` — use `millis()` timing

## Core Principles

1. **Minimize Changes** — Only modify what's necessary
2. **Utilize Git** - Follow the instructions in `git-standards.md`
3. **Educate Team** — Explain new embedded/C++ concepts
4. **Optimize Memory** — Flash for constants, minimize SRAM
5. **Protect Hardware** — Validate angles, pins, etc.
6. **Non-Blocking Code** — Use `millis()`, avoid `delay()` in loop
7. **Consistent Style** — Match existing patterns
8. **Document Clearly** — Update comments and docs