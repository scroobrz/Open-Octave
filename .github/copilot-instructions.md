# Copilot Instructions: Open Octave Code Review

**Context:** Open Octave is a modular, robotically enhanced educational keyboard system defined in `docs/planning/Project-Plan.pdf`. The system involves mechanical keys, robotic actuation (servos), visual feedback (LEDs), and audio.

**Primary Goal:** Your role is to ensure code quality, safety, and architectural integrity as the project scales from a single prototype to a multi-module distributed system.

## 1. Safety & Physical Constraints (Critical)
**WARNING:** This code controls physical hardware. Bad logic can break components.
-   **Servo Limits:** ALWAYS verify that servo angles are constrained to safe limits (typically defined in config as rest/press angles). Flag any code that sets arbitrary angles without bounds checking to prevent mechanical stalling.
-   **Blocking Code:** In a real-time system (music), `delay()` is dangerous. Flag excessive use of blocking delays that could disrupt audio generation, input polling, or network heartbeats. Suggest non-blocking alternatives (state machines using `millis()`).
-   **Power Management:** Be wary of loops that might turn on all actuators simultaneously without purpose, as this could overload the power supply.

## 2. Architectural Consistency
Ensure implementation aligns with the three core operating modes defined in the Project Plan:
1.  **Piano Mode:** Passive input (User presses key -> Sound/Action).
2.  **Guided Mode:** Reactive (LED lights -> Wait for User press -> Next step).
3.  **Teaching Mode:** Active (Servo presses key + LED -> Sound).

**Review Check:**
-   Does the code strictly adhere to the active `Mode`?
-   Are transitions between modes clean (e.g., resetting servos/LEDs to default states)?
-   Is the "State Machine" logic distinct and well-structured?

## 3. Modularity & Scalability
The system is designed to connect multiple "Octave Modules" together (e.g., creating a 2-octave piano).
-   **Avoid Magic Numbers:** Flag hardcoded values like `3`, `8`, or `12` for key counts. Use constants (e.g., `NUM_KEYS`) to allow easy scaling to larger setups.
-   **Hardware Abstraction:** Logic should be separated from pin definitions. Encourage moving pin configs to header files (like `config.h`), facilitating the planned migration from Arduino Uno to ESP32.
-   **Inter-Module Logic:** When reviewing networking code, ensure it supports the concept of "Module ID" or "Octave Offset" so keys are mapped correctly across multiple devices.

## 4. Code Style & Best Practices
-   **Target Audience:** The team includes students with varying hardware experience. Code must be highly readable and self-documenting.
-   **Variable Naming:** Must be semantic (e.g., `ledStripPin` is better than `pin6`).
-   **C++ / Arduino:**
    -   Verify correct usage of `setup()` (initialization) and `loop()` (runtime).
    -   Check for proper memory management (avoid dynamic allocation like `new` or `String` concatenation in `loop()` on microcontrollers).
    -   Encourage `const` correctness for hardware pins and configuration values.

## 5. Build & Validation (Static Analysis)
**Note:** The environment often lacks a live compiler.
-   **Syntax Check:** Rigorously check for missing semicolons, mismatched braces, and correct type usage.
-   **Library Usage:** Verify correct API usage for key libraries (`Adafruit_NeoPixel`, `PCA9685`, `Wire`) based on standard signatures.
-   **Pin Conflicts:** Check that the same pin is not assigned to multiple peripherals (e.g., a button and an LED sharing a GPIO).