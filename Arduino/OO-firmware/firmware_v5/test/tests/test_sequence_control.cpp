#include <gtest/gtest.h>
#include "Arduino.h"
#include "globals.h"
#include "firmware_V5_config.h"
#include "sequence_control_testable.h"
#include "clsPCA9555_mock.h"

namespace {

class SequenceControlTest : public ::testing::Test {
protected:
 void SetUp() override {
 sequenceRunning = false;
 recording = false;
 currentSequenceStepIndex = 0;
 currentStepStartTime = 0;
 lastSequenceButtonPressTime = 0;
 currentSequenceMode = GUIDED;
 currentSequence.length = 0;
 memset(currentSequence.name, 0, sizeof(currentSequence.name));
 mock_millis_set_time(0);
 ioport.digitalWrite(GUIDED_SEQ_BUTTON_PIN, LOW);
 ioport.digitalWrite(TEACHING_SEQ_BUTTON_PIN, LOW);
 }
};

TEST_F(SequenceControlTest, handleSequenceButtons_InitialState) {
 handleSequenceButtons();
 ASSERT_FALSE(sequenceRunning);
}

TEST_F(SequenceControlTest, handleSequenceButtons_StartsGuidedMode) {
 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY + 10);
 loadDefaultSequence();
 ASSERT_GT(currentSequence.length, 0);

 ioport.digitalWrite(GUIDED_SEQ_BUTTON_PIN, HIGH);
 handleSequenceButtons();

 ASSERT_TRUE(sequenceRunning);
 ASSERT_EQ(currentSequenceMode, GUIDED);
}

TEST_F(SequenceControlTest, handleSequenceButtons_StartsTeachingMode) {
 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY + 10);
 loadDefaultSequence();
 ASSERT_GT(currentSequence.length, 0);

 ioport.digitalWrite(TEACHING_SEQ_BUTTON_PIN, HIGH);
 handleSequenceButtons();

 ASSERT_TRUE(sequenceRunning);
 ASSERT_EQ(currentSequenceMode, TEACHING);
}

TEST_F(SequenceControlTest, handleSequenceButtons_IgnoresPressDuringDebounce) {
 loadDefaultSequence();
 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY + 10);
 ioport.digitalWrite(GUIDED_SEQ_BUTTON_PIN, HIGH);
 handleSequenceButtons();
 ASSERT_TRUE(sequenceRunning);

 stopSequence();
 ASSERT_FALSE(sequenceRunning);

 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY + 30);
 ioport.digitalWrite(GUIDED_SEQ_BUTTON_PIN, HIGH);
 handleSequenceButtons();
 ASSERT_FALSE(sequenceRunning);
}

TEST_F(SequenceControlTest, handleSequenceButtons_DoesNotActWhenSequenceRunning) {
 loadDefaultSequence();
 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY + 10);
 ioport.digitalWrite(GUIDED_SEQ_BUTTON_PIN, LOW);
 handleSequenceButtons();
 ioport.digitalWrite(GUIDED_SEQ_BUTTON_PIN, HIGH);
 handleSequenceButtons();
 ASSERT_TRUE(sequenceRunning);

 mock_millis_set_time(2 * BUTTON_DEBOUNCE_DELAY + 20);
 ioport.digitalWrite(TEACHING_SEQ_BUTTON_PIN, HIGH);
 handleSequenceButtons();
 ASSERT_TRUE(sequenceRunning);
 ASSERT_EQ(currentSequenceMode, GUIDED);
}

TEST_F(SequenceControlTest, handleSequenceButtons_DoesNotActWhenRecording) {
 recording = true;
 ioport.digitalWrite(GUIDED_SEQ_BUTTON_PIN, LOW);

 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY + 10);
 ioport.digitalWrite(GUIDED_SEQ_BUTTON_PIN, HIGH);
 handleSequenceButtons();
 ASSERT_FALSE(sequenceRunning);

 recording = false;
 loadDefaultSequence();
 ioport.digitalWrite(GUIDED_SEQ_BUTTON_PIN, LOW);
 handleSequenceButtons();

 mock_millis_set_time(2 * BUTTON_DEBOUNCE_DELAY + 20);
 ioport.digitalWrite(GUIDED_SEQ_BUTTON_PIN, HIGH);
 handleSequenceButtons();
 ASSERT_TRUE(sequenceRunning);
}

TEST_F(SequenceControlTest, startSequence_InvalidLength) {
 currentSequence.length = 0;
 startSequence(GUIDED);
 ASSERT_FALSE(sequenceRunning);
}

TEST_F(SequenceControlTest, startSequence_ValidSequence) {
 loadDefaultSequence();
 ASSERT_GT(currentSequence.length, 0);

 mock_millis_set_time(1000);
 startSequence(GUIDED);

 ASSERT_TRUE(sequenceRunning);
 ASSERT_EQ(currentSequenceMode, GUIDED);
 ASSERT_EQ(currentSequenceStepIndex, 0);
 ASSERT_EQ(currentStepStartTime, 1000);
}

TEST_F(SequenceControlTest, startSequence_AlreadyRunning) {
 loadDefaultSequence();
 startSequence(GUIDED);
 ASSERT_TRUE(sequenceRunning);

 mock_millis_set_time(2000);
 SequenceMode savedMode = currentSequenceMode;
 startSequence(TEACHING);

 ASSERT_TRUE(sequenceRunning);
 ASSERT_EQ(currentSequenceMode, savedMode);
}

TEST_F(SequenceControlTest, stopSequence_StopsRunning) {
 loadDefaultSequence();
 startSequence(GUIDED);
 ASSERT_TRUE(sequenceRunning);

 mock_millis_set_time(3000);
 stopSequence();

 ASSERT_FALSE(sequenceRunning);
 ASSERT_FALSE(waitingForServoRelease);
}

TEST_F(SequenceControlTest, stopSequence_DoesNothingWhenNotRunning) {
 sequenceRunning = false;
 stopSequence();
 ASSERT_FALSE(sequenceRunning);
}

TEST_F(SequenceControlTest, loadDefaultSequence_CreatesValidSequence) {
 ASSERT_EQ(currentSequence.length, 0);

 loadDefaultSequence();

 ASSERT_EQ(currentSequence.id, 0);
 ASSERT_STREQ(currentSequence.name, "Default");
 ASSERT_GT(currentSequence.length, 0);
 ASSERT_LT(currentSequence.length, MAX_SEQUENCE_LENGTH);
 ASSERT_GT(currentSequence.steps[0].numKeys, 0);
 ASSERT_LE(currentSequence.steps[0].numKeys, MAX_KEYS_PER_STEP);
 ASSERT_GT(currentSequence.steps[0].duration, 0);
}

TEST_F(SequenceControlTest, handleSequencePlayback_DoesNothingWhenNotRunning) {
 sequenceRunning = false;
 handleSequencePlayback();
 ASSERT_FALSE(sequenceRunning);
}

TEST_F(SequenceControlTest, handleSequencePlayback_StopsOnInvalidStepIndex) {
 loadDefaultSequence();
 startSequence(TEACHING);
 ASSERT_TRUE(sequenceRunning);

 currentSequenceStepIndex = -1;
 handleSequencePlayback();

 ASSERT_FALSE(sequenceRunning);
}

TEST_F(SequenceControlTest, stopSequence_PlayAndStopMultipleTimes) {
 loadDefaultSequence();
 startSequence(GUIDED);
 ASSERT_TRUE(sequenceRunning);
 stopSequence();
 ASSERT_FALSE(sequenceRunning);

 mock_millis_set_time(5000);
 loadDefaultSequence();
 startSequence(TEACHING);
 ASSERT_TRUE(sequenceRunning);
 ASSERT_EQ(currentSequenceMode, TEACHING);
 stopSequence();
 ASSERT_FALSE(sequenceRunning);
}

TEST_F(SequenceControlTest, startSequence_MultipleStartsWithDifferentModes) {
 loadDefaultSequence();
 startSequence(GUIDED);
 ASSERT_TRUE(sequenceRunning);
 ASSERT_EQ(currentSequenceMode, GUIDED);

 stopSequence();
 ASSERT_FALSE(sequenceRunning);

 mock_millis_set_time(5000);
 startSequence(TEACHING);
 ASSERT_TRUE(sequenceRunning);
 ASSERT_EQ(currentSequenceMode, TEACHING);
}

} // namespace
