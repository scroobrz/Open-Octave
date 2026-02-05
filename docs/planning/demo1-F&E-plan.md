# Firmware & Electronics Plan for Demo 1

## Firmware
- Serial Interface: **Robin**
    - Implement debugging logs throughout the code
    - Allow for serial commands to control the keyboard via USB and replace the current mode switch button
- Refactor to control separate LED strips instead of different sections of the same strip
- Add more robust error checking throughout the code in case of misconfiguration of parameters
- Possibly expand to 3 keys once the mode switch button is gone
- Configure more sequences for the demo
- Test sequences with repeated steps

## Electronics
- If extending to a third key, need a new servo and button
- Breadboard: **Tommy and Ahmad**
    - Continue figuring out how they work
    - Learn how to connect grove components to breadboard
    - Design overall PCB for demo 1
- LEDs: **Alvin and Shuoshuo**
    - Cut into 3 strips
    - Remove outer casing (silicone waterproof layer)
    - Solder wires to each strip
    - Test that they work separately
- Integrate everything with the keys
- Put together shopping list for demo 2
    - Speaker(s)
    - Audio amplifier(s)
    - Skinny LED strips if needed
    - ESP32 modules if needed (grove shield?)

## Other tasks
- Create demo materials (report and presentation)
    - Laurie will take charge of creating presentation materials
    - Robin will write the project management update in the report
    - Ao will write the testing section of the report
    - Budget and miscellaneous sections to be assigned