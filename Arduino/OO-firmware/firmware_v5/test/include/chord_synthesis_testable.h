#ifndef CHORD_SYNTHESIS_TESTABLE_H
#define CHORD_SYNTHESIS_TESTABLE_H

#include <cstdint>

void buildSineTable();
void noteSetup();
void playPressedKeys();

// Test utility functions
float getSineTableValue(int index);
float getPhaseAccumulator(int keyIndex);
void resetPhaseAccumulators();
int16_t getAudioSample(int index); // Get generated audio sample

#endif // CHORD_SYNTHESIS_TESTABLE_H
