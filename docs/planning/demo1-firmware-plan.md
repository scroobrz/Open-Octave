# Firmware & Electronics Plan for Demo 1

## Current Code Summary

`firmware_v1.cpp` currently:
- Detects a button press
- Moves one servo between 0° and 90°
- Lights up the whole LED strip with a rainbow
- Makes a sound

## Electronics Setup Instructions

To reproduce the current functionality:

- Plug arduino into computer via USB
- Connect the power supply to the motor control board
- Upload `firmware_v1.cpp` to the arduino (turn it into a .ino file in the arduino IDE first)

Everything else should already be connected.

## Next Steps

### Firmware
- Right now everything is hardcoded for one key. We need to support 2+ keys.
    - Create a `Key` struct that stores: servo channel, button pin, LED index, note frequency
    - Create an array of keys (one entry per physical key)
    - Each key operates independently
- When a button is pressed (by human or by servo), play a dedicated sound for the corresponding key.
- Create a function to automatically move specific servos and light up their corresponding LEDs at the same time.
- Create a function to automatically light up specific LEDs without moving the servos.
- Allow the above to happen and be activated in specific sequences.
- Allow switching between automatic and manual mode.

### Electronics

- Connect a new button, servo and LED stick for each key
- Figure out how to accommodate enough buttons, and LED sticks (servos should be fine tho due to the motor control board)
- Figure out how to set everything up with a breadboard (preferably the Grove breadboard but we also have a larger regular breadboard) for our first PCB prototype

## General Notes

Try to stay within the Grove ecosystem as much as possible with the electronics, this makes life a lot easier with their sort of plug-and-play nature