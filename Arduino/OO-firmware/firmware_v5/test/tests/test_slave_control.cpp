#include <gtest/gtest.h>
#include "Arduino.h"
#include "globals.h"
#include "slave_control_testable.h"

namespace {

class SlaveControlTest : public ::testing::Test {
protected:
    void SetUp() override {
        isMaster = true;
        moduleChainIndex = 0;
        chainBaseOctave = 4;
        // Initialize base note frequencies for testing
        for (int i = 0; i < NUM_KEYS; i++) {
            keys[i].noteFreq = 100 * (i + 1); // Simple progression
        }
    }
};

TEST_F(SlaveControlTest, DemoteToSlave_SetsSlaveState) {
    ASSERT_TRUE(isMaster);

    demoteToSlave();

    ASSERT_FALSE(isMaster);
}

TEST_F(SlaveControlTest, ConfigureNotes_OctaveShiftForModuleIndex0) {
    moduleChainIndex = 0;

    configureNotes();

    // For module 0, frequency should not be shifted (<< 0 = no shift)
    ASSERT_EQ(keys[0].noteFreq, baseNoteFreqs[0]);
}

TEST_F(SlaveControlTest, ConfigureNotes_OctaveShiftForModuleIndex1) {
    moduleChainIndex = 1;

    configureNotes();

    // For module 1, frequency should be doubled (<< 1 = multiply by 2)
    ASSERT_EQ(keys[0].noteFreq, 262 << 1);
    ASSERT_EQ(keys[1].noteFreq, 277 << 1);
    ASSERT_EQ(keys[2].noteFreq, 294 << 1);
}

TEST_F(SlaveControlTest, ConfigureNotes_OctaveShiftForModuleIndex2) {
    moduleChainIndex = 2;

    configureNotes();

    // For module 2, frequency should be quadrupled (<< 2 = multiply by 4)
    ASSERT_EQ(keys[0].noteFreq, 262 << 2);
    ASSERT_EQ(keys[1].noteFreq, 277 << 2);
}

TEST_F(SlaveControlTest, ConfigureNotes_OctaveShiftForModuleIndex3) {
    moduleChainIndex = 3;

    configureNotes();

    // For module 3, frequency should be multiplied by 8 (<< 3)
    ASSERT_EQ(keys[0].noteFreq, 262 << 3);
    ASSERT_EQ(keys[11].noteFreq, 494 << 3);
}

TEST_F(SlaveControlTest, ConfigureNotes_AllKeysShifted) {
    moduleChainIndex = 1;

    configureNotes();

    // Verify all keys are shifted
    for (int i = 0; i < NUM_KEYS; i++) {
        ASSERT_GT(keys[i].noteFreq, 0);
    }
}

TEST_F(SlaveControlTest, ConfigureNotes_MultipleCallsUpdateCorrectly) {
    // configure with module 1
    moduleChainIndex = 1;
    configureNotes();
    int noteAfterFirst = keys[0].noteFreq;

    // reconfigure with module 2
    moduleChainIndex = 2;
    configureNotes();
    int noteAfterSecond = keys[0].noteFreq;

    // Should be different after reconfiguration
    ASSERT_NE(noteAfterFirst, noteAfterSecond);
    ASSERT_EQ(noteAfterSecond, 262 << 2);
}

TEST_F(SlaveControlTest, DemoteToSlave_ConfiguresStateCorrectly) {
    // Ensure state is properly set when becoming slave
    moduleChainIndex = 2;

    demoteToSlave();

    ASSERT_FALSE(isMaster);
    // moduleIndex should be preserved
    ASSERT_EQ(moduleChainIndex, 2);
}

TEST_F(SlaveControlTest, ConfigureNotes_SlaveDerivesFromChainBase) {
    isMaster = false;
    chainBaseOctave = 5;       // Master is at octave 5
    moduleChainIndex = 1;      // This is the first slave
    
    configureNotes();
    
    // Slave should be at octave 6 (5 + 1), shift = +2 from base octave 4
    ASSERT_EQ(currentEffectiveOctave, 6);
    ASSERT_EQ(keys[0].noteFreq, 262 << 2);
}

TEST_F(SlaveControlTest, ConfigureNotes_SlaveOctaveCappedAt7) {
    isMaster = false;
    chainBaseOctave = 6;       // Master is at octave 6
    moduleChainIndex = 2;      // This is the second slave (6 + 2 = 8, should cap at 7)
    
    configureNotes();
    
    ASSERT_EQ(currentEffectiveOctave, 7);
    ASSERT_EQ(keys[0].noteFreq, 262 << 3);  // shift = 7 - 4 = 3
}

TEST_F(SlaveControlTest, ConfigureNotes_MasterUsesChainBase) {
    isMaster = true;
    chainBaseOctave = 3;       // Master configured to octave 3
    moduleChainIndex = 0;
    
    configureNotes();
    
    // Master's effective octave should equal the chain base
    ASSERT_EQ(currentEffectiveOctave, 3);
    ASSERT_EQ(keys[0].noteFreq, 262 >> 1);  // shift = 3 - 4 = -1
}

} // namespace

