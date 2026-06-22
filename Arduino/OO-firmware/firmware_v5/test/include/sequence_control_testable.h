#ifndef SEQUENCE_CONTROL_TESTABLE_H
#define SEQUENCE_CONTROL_TESTABLE_H

// Testable version of sequence_control.ino functions
// Copy the implementations here from sequence_control.ino

#include "firmware_types.h"

void handleSequenceButtons();
void handleSequencePlayback();
void handleTeachingModePlayback();
void handleGuidedModePlayback();
void startSequence(SequenceMode mode);
void stopSequence();
void executeCurrentSequenceStep();
void evaluateWrongKeyFeedback(int globalKey, bool isPressed);
void loadDefaultSequence();

// Helper functions that need to be stubbed for tests
const char* getCurrentSequenceModeString();
const char* getColorString(uint8_t color);
const char* getColorString(uint8_t color);
bool isValidGlobalKeyIndex(int keyIndex);

#endif // SEQUENCE_CONTROL_TESTABLE_H