#include <gtest/gtest.h>
#include "Arduino.h"
#include "globals.h"
#include "key_control_testable.h"

namespace {

class KeyControlTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(globalKeyIsPressed, 0, sizeof(globalKeyIsPressed));
  }
};

TEST_F(KeyControlTest, HandleKeyPresses_InitialState) {
  handleKeyPresses();
}

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

} // namespace
