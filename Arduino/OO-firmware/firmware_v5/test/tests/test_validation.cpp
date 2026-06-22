#include <gtest/gtest.h>
#include "Arduino.h"
#include "globals.h"
#include "validation_testable.h"
#include "../include/keys.h"

namespace {

class ValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize keys with valid values for NUM_KEYS=12
        for (int i = 0; i < NUM_KEYS; i++) {
            keys[i].buttonPin = i;
            keys[i].ledIndex = i;
            keys[i].servoChannel = i + 1;
            keys[i].noteFreq = 262 + (i * 10);
        }
        numModulesInChain = 1; // Default to single module
    }
};

TEST_F(ValidationTest, ValidateHardwareInit_ReturnsTrue) {
    bool result = validateHardwareInit();
    ASSERT_TRUE(result);
}

TEST_F(ValidationTest, IsValidLocalKeyIndex_ValidIndices) {
    // Test valid local key indices (0-11 for NUM_KEYS=12)
    ASSERT_TRUE(isValidLocalKeyIndex(0));
    ASSERT_TRUE(isValidLocalKeyIndex(5));
    ASSERT_TRUE(isValidLocalKeyIndex(11));
}

TEST_F(ValidationTest, IsValidLocalKeyIndex_InvalidIndices) {
    // Test invalid local key indices
    ASSERT_FALSE(isValidLocalKeyIndex(-1));
    ASSERT_FALSE(isValidLocalKeyIndex(-10));
    ASSERT_FALSE(isValidLocalKeyIndex(12)); // First invalid index for NUM_KEYS=12
    ASSERT_FALSE(isValidLocalKeyIndex(20));
}

TEST_F(ValidationTest, IsValidLocalKeyIndex_BoundaryConditions) {
    // Test boundary conditions
    ASSERT_FALSE(isValidLocalKeyIndex(-1));
    ASSERT_TRUE(isValidLocalKeyIndex(0));
    ASSERT_TRUE(isValidLocalKeyIndex(11)); // Last valid for NUM_KEYS=12
    ASSERT_FALSE(isValidLocalKeyIndex(12)); // First invalid
}

TEST_F(ValidationTest, IsValidGlobalKeyIndex_ValidIndices_SingleModule) {
    // Test valid global key indices for 1 module (0-11)
    numModulesInChain = 1;
    ASSERT_TRUE(isValidGlobalKeyIndex(0));
    ASSERT_TRUE(isValidGlobalKeyIndex(11));
    ASSERT_FALSE(isValidGlobalKeyIndex(12));
}

TEST_F(ValidationTest, IsValidGlobalKeyIndex_ValidIndices_MultipleModules) {
    // Test valid global key indices for 4 modules (0-47)
    numModulesInChain = 4;
    ASSERT_TRUE(isValidGlobalKeyIndex(0));
    ASSERT_TRUE(isValidGlobalKeyIndex(11));
    ASSERT_TRUE(isValidGlobalKeyIndex(12));
    ASSERT_TRUE(isValidGlobalKeyIndex(23));
    ASSERT_TRUE(isValidGlobalKeyIndex(47));
}

TEST_F(ValidationTest, IsValidGlobalKeyIndex_InvalidIndices) {
    numModulesInChain = 4;
    ASSERT_FALSE(isValidGlobalKeyIndex(-1));
    ASSERT_FALSE(isValidGlobalKeyIndex(-10));
    ASSERT_FALSE(isValidGlobalKeyIndex(48)); // First invalid index for 4 modules
    ASSERT_FALSE(isValidGlobalKeyIndex(100));
}

TEST_F(ValidationTest, IsValidGlobalKeyIndex_BoundaryConditions) {
    numModulesInChain = 4;
    ASSERT_FALSE(isValidGlobalKeyIndex(-1));
    ASSERT_TRUE(isValidGlobalKeyIndex(0));
    ASSERT_TRUE(isValidGlobalKeyIndex(47)); // Last valid index
    ASSERT_FALSE(isValidGlobalKeyIndex(48)); // First invalid index
}

TEST_F(ValidationTest, ValidateHardwareInit_InvalidButtonPin) {
    keys[0].buttonPin = -1;
    bool result = validateHardwareInit();
    ASSERT_FALSE(result);
}

TEST_F(ValidationTest, ValidateHardwareInit_InvalidLedIndex) {
    keys[0].ledIndex = NUM_KEYS + 5;
    bool result = validateHardwareInit();
    ASSERT_FALSE(result);
}

TEST_F(ValidationTest, ValidateHardwareInit_InvalidServoChannel) {
    keys[0].servoChannel = 50;
    bool result = validateHardwareInit();
    ASSERT_FALSE(result);
}

TEST_F(ValidationTest, ValidateHardwareInit_InvalidNoteFreq) {
    keys[0].noteFreq = -100;
    bool result = validateHardwareInit();
    ASSERT_FALSE(result);
}

TEST_F(ValidationTest, IsValidLocalKeyIndex_LargeNegativeValue) {
    ASSERT_FALSE(isValidLocalKeyIndex(INT_MIN));
}

TEST_F(ValidationTest, IsValidLocalKeyIndex_LargePositiveValue) {
    ASSERT_FALSE(isValidLocalKeyIndex(INT_MAX));
    ASSERT_FALSE(isValidLocalKeyIndex(1000000));
}

TEST_F(ValidationTest, IsValidLocalKeyIndex_ZeroValue) {
    ASSERT_TRUE(isValidLocalKeyIndex(0));
}

TEST_F(ValidationTest, IsValidGlobalKeyIndex_LargeNegativeValue) {
    ASSERT_FALSE(isValidGlobalKeyIndex(INT_MIN));
}

TEST_F(ValidationTest, IsValidGlobalKeyIndex_LargePositiveValue) {
    ASSERT_FALSE(isValidGlobalKeyIndex(INT_MAX));
    ASSERT_FALSE(isValidGlobalKeyIndex(1000000));
}

TEST_F(ValidationTest, IsValidGlobalKeyIndex_ZeroValue) {
    ASSERT_TRUE(isValidGlobalKeyIndex(0));
}

TEST_F(ValidationTest, ValidateHardwareInit_ConsistentResult) {
    // Call multiple times and ensure consistent results
    bool result1 = validateHardwareInit();
    bool result2 = validateHardwareInit();
    ASSERT_EQ(result1, result2);
    ASSERT_TRUE(result1);
    ASSERT_TRUE(result2);
}

} // namespace
