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

TEST_F(HelpersTest, ToLowercase_LeavesLowercaseUnchanged) {
    char test = 'b';
    toLowercase(test);
    ASSERT_EQ(test, 'b');
}

TEST_F(HelpersTest, ToLowercase_LeavesNonLettersUnchanged) {
    char test = '1';
    toLowercase(test);
    ASSERT_EQ(test, '1');
}

TEST_F(HelpersTest, GetColorString_ReturnsCorrectNames) {
    ASSERT_STREQ(getColorString(COLOR_RED), "RED");
    ASSERT_STREQ(getColorString(COLOR_GREEN), "GREEN");
    ASSERT_STREQ(getColorString(COLOR_BLUE), "BLUE");
    ASSERT_STREQ(getColorString(COLOR_ORANGE), "ORANGE");
    ASSERT_STREQ(getColorString(COLOR_YELLOW), "YELLOW");
    ASSERT_STREQ(getColorString(COLOR_MAGENTA), "MAGENTA");
    ASSERT_STREQ(getColorString(COLOR_WHITE), "WHITE");
}

TEST_F(HelpersTest, GetColorString_ReturnsCustomForUnknownColors) {
    ASSERT_STREQ(getColorString(0x123456), "CUSTOM");
    ASSERT_STREQ(getColorString(0xABCDEF), "CUSTOM");
}

} // namespace
