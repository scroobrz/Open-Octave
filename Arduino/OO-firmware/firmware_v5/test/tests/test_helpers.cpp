#include <gtest/gtest.h>
#include "Arduino.h"
#include "globals.h"
#include "helpers_testable.h"

namespace {

class HelpersTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Setup test environment
  }
};

TEST_F(HelpersTest, CheckUpstream_ReturnsFalseInTestMode) {
  bool result = checkUpstream();
  ASSERT_FALSE(result);
}

TEST_F(HelpersTest, GetKeyIndex_ReturnsValidIndex) {
  uint8_t index = getKeyIndex(0, 0);
  ASSERT_EQ(index, 0);
}

TEST_F(HelpersTest, GetKeyPosition_ReturnsValidPosition) {
  uint8_t position = getKeyPosition(0);
  ASSERT_EQ(position, 0);
}

TEST_F(HelpersTest, GetKeyFrequency_ReturnsValidFrequency) {
  uint16_t freq = getKeyFrequency(0);
  ASSERT_GT(freq, 0);
}

TEST_F(HelpersTest, ToLowercase_ConvertsUppercase) {
  char test = 'A';
  toLowercase(test);
  ASSERT_EQ(test, 'a');
}

TEST_F(HelpersTest, IsValidGlobalKeyIndex_ValidKeyReturnsTrue) {
  ASSERT_TRUE(isValidGlobalKeyIndex(0));
  ASSERT_TRUE(isValidGlobalKeyIndex(MAX_TOTAL_KEYS - 1));
}

TEST_F(HelpersTest, IsValidGlobalKeyIndex_InvalidKeyReturnsFalse) {
  ASSERT_FALSE(isValidGlobalKeyIndex(-1));
  ASSERT_FALSE(isValidGlobalKeyIndex(MAX_TOTAL_KEYS));
}

} // namespace
