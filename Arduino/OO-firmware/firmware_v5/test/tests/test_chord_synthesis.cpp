#include <gtest/gtest.h>
#include "Arduino.h"
#include "globals.h"
#include "chord_synthesis_testable.h"
#include <cmath>

namespace {

class ChordSynthesisTest : public ::testing::Test {
protected:
    void SetUp() override {
        memset(keys, 0, sizeof(keys));
        for (int i = 0; i < NUM_KEYS; i++) {
            keys[i].noteFreq = 100 * (i + 1);
        }
        resetPhaseAccumulators();
    }
};

TEST_F(ChordSynthesisTest, BuildSineTable_ValuesInCorrectRange) {
    buildSineTable();
    for (int i = 0; i < 1024; i++) {
        float value = getSineTableValue(i);
        ASSERT_GE(value, -1.0f);
        ASSERT_LE(value, 1.0f);
    }
}

TEST_F(ChordSynthesisTest, PlayPressedKeys_SingleKeyPressed) {
    keys[0].isPressed = true;
    keys[0].noteFreq = 440.0f;
    playPressedKeys();
    float phase = getPhaseAccumulator(0);
    ASSERT_GT(phase, 0.0f);
}

TEST_F(ChordSynthesisTest, FullSynthesisSequence) {
    buildSineTable();
    keys[0].isPressed = keys[3].isPressed = keys[7].isPressed = true;
    playPressedKeys();
    bool audio = false;
    for (int i = 0; i < 100; i++) if (getAudioSample(i) != 0) { audio = true; break; }
    ASSERT_TRUE(audio);
}

TEST_F(ChordSynthesisTest, DifferentFrequencies) {
    buildSineTable();
    keys[0].isPressed = true; keys[0].noteFreq = 100.0f;
    keys[1].isPressed = true; keys[1].noteFreq = 200.0f;
    playPressedKeys();
    ASSERT_NE(getAudioSample(0), 0);
}

} // namespace
