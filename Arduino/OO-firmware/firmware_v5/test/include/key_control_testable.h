#ifndef KEY_CONTROL_TESTABLE_H
#define KEY_CONTROL_TESTABLE_H

#include <stdint.h>

void handleKeyPresses();
void stopKeyTone(int globalKey);
void lightUpKey(int globalKey, uint32_t color);
void lightDownKey(int globalKey);
void resetKey(int globalKey);
void playStartupAnimation();
void playShutdownAnimation();

#endif // KEY_CONTROL_TESTABLE_H
