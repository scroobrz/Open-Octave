#include <gtest/gtest.h>
#include "Arduino.h"
#include "globals.h"
#include "key_control_testable.h"
#include "helpers_testable.h"
#include "firmware_V5_config.h"
#include "../mocks/clsPCA9555_mock.h"

namespace {

class KeyControlTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(globalKeyIsPressed, 0, sizeof(globalKeyIsPressed));
  }
};

TEST_F(KeyControlTest, StopKeyTone_NoToneGenerated) {
  stopKeyTone(0);
}

TEST_F(KeyControlTest, LightUpKey_ChangesLEDState) {
  lightUpKey(0, COLOR_RED);
}

TEST_F(KeyControlTest, LightDownKey_ReturnsToNormal) {
  lightDownKey(0);
}

TEST_F(KeyControlTest, ResetKey_InitializesHardware) {
  resetKey(0);
}

TEST_F(KeyControlTest, PlayStartupAnimation_CompletesWithoutError) {
  playStartupAnimation();
}

TEST_F(KeyControlTest, PlayShutdownAnimation_CompletesWithoutError) {
  playShutdownAnimation();
}

TEST_F(KeyControlTest, ToLowercase) {
  char upper = 'Z';
  toLowercase(upper);
  ASSERT_EQ(upper, 'z');

  char lower = 'a';
  toLowercase(lower);
  ASSERT_EQ(lower, 'a'); // Should remain lowercase
}

TEST_F(KeyControlTest, GetColorString) {
  const char* red = getColorString(COLOR_RED);
  ASSERT_STREQ(red, "RED");

  const char* blue = getColorString(COLOR_BLUE);
  ASSERT_STREQ(blue, "BLUE");

  const char* unknown = getColorString(0x123456);
  ASSERT_STREQ(unknown, "CUSTOM");
}

} // namespace
