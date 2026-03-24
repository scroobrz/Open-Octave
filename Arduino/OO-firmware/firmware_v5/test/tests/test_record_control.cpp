#include <gtest/gtest.h>
#include "Arduino.h"
#include "globals.h"
#include "firmware_V5_config.h"
#include "record_control_testable.h"
#include "clsPCA9555_mock.h"

namespace {

class RecordControlTest : public ::testing::Test {
protected:
 void SetUp() override {
 recording = false;
 sequenceRunning = false;
 uploadingSequence = false;
 lastSequenceButtonPressTime = 0;
 lastRecordButtonState = false;
 recStepCount = 0;
 recChordNumKeys = 0;
 recChordStartTime = 0;
 mock_millis_set_time(0);
 ioport.digitalWrite(RECORD_BUTTON_PIN, LOW);
 // Clear chord keys array
 memset(recChordKeys, 0, sizeof(recChordKeys));
 // Clear current sequence
 memset(&currentSequence, 0, sizeof(currentSequence));
 }
};

TEST_F(RecordControlTest, handleRecordButton_InitialState) {
 handleRecordButton();
 ASSERT_FALSE(recording);
}

TEST_F(RecordControlTest, handleRecordButton_TogglesRecordingOnButtonPress) {
 ioport.digitalWrite(RECORD_BUTTON_PIN, LOW);
 handleRecordButton();
 ASSERT_FALSE(recording);

 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY + 10);
 ioport.digitalWrite(RECORD_BUTTON_PIN, HIGH);
 handleRecordButton();
 ASSERT_TRUE(recording);

 mock_millis_set_time(2 * BUTTON_DEBOUNCE_DELAY + 20);
 ioport.digitalWrite(RECORD_BUTTON_PIN, LOW);
 handleRecordButton();

 mock_millis_set_time(3 * BUTTON_DEBOUNCE_DELAY + 30);
 ioport.digitalWrite(RECORD_BUTTON_PIN, HIGH);
 handleRecordButton();
 ASSERT_FALSE(recording);
}

TEST_F(RecordControlTest, handleRecordButton_IgnoresPressDuringDebouncePeriod) {
 ioport.digitalWrite(RECORD_BUTTON_PIN, LOW);
 lastSequenceButtonPressTime = 0;
 handleRecordButton();
 ASSERT_FALSE(recording);

 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY / 2);
 ioport.digitalWrite(RECORD_BUTTON_PIN, HIGH);
 handleRecordButton();
 ASSERT_FALSE(recording);

 mock_millis_set_time(2 * BUTTON_DEBOUNCE_DELAY);
 ioport.digitalWrite(RECORD_BUTTON_PIN, LOW);
 handleRecordButton();

 ioport.digitalWrite(RECORD_BUTTON_PIN, HIGH);
 handleRecordButton();
 ASSERT_TRUE(recording);
}

TEST_F(RecordControlTest, handleRecordButton_DoesNotActWhenSequenceRunning) {
 sequenceRunning = true;
 ioport.digitalWrite(RECORD_BUTTON_PIN, LOW);

 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY + 10);
 ioport.digitalWrite(RECORD_BUTTON_PIN, HIGH);
 handleRecordButton();
 ASSERT_FALSE(recording);

 sequenceRunning = false;
 ioport.digitalWrite(RECORD_BUTTON_PIN, LOW);
 handleRecordButton();

 mock_millis_set_time(2 * BUTTON_DEBOUNCE_DELAY + 20);
 ioport.digitalWrite(RECORD_BUTTON_PIN, HIGH);
 handleRecordButton();
 ASSERT_TRUE(recording);
}

TEST_F(RecordControlTest, handleRecordButton_DoesNotActWhenUploadingSequence) {
 uploadingSequence = true;
 ioport.digitalWrite(RECORD_BUTTON_PIN, LOW);

 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY + 10);
 ioport.digitalWrite(RECORD_BUTTON_PIN, HIGH);
 handleRecordButton();
 ASSERT_FALSE(recording);

 uploadingSequence = false;
 ioport.digitalWrite(RECORD_BUTTON_PIN, LOW);
 handleRecordButton();

 mock_millis_set_time(2 * BUTTON_DEBOUNCE_DELAY + 20);
 ioport.digitalWrite(RECORD_BUTTON_PIN, HIGH);
 handleRecordButton();
 ASSERT_TRUE(recording);
}

TEST_F(RecordControlTest, handleRecordButton_IgnoresButtonHold) {
 ioport.digitalWrite(RECORD_BUTTON_PIN, LOW);

 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY + 10);
 ioport.digitalWrite(RECORD_BUTTON_PIN, HIGH);
 handleRecordButton();
 ASSERT_TRUE(recording);

 mock_millis_set_time(BUTTON_DEBOUNCE_DELAY + 100);
 ioport.digitalWrite(RECORD_BUTTON_PIN, HIGH);
 handleRecordButton();
 ASSERT_TRUE(recording);

 mock_millis_set_time(2 * BUTTON_DEBOUNCE_DELAY + 20);
 ioport.digitalWrite(RECORD_BUTTON_PIN, LOW);
 handleRecordButton();

 mock_millis_set_time(3 * BUTTON_DEBOUNCE_DELAY + 30);
 ioport.digitalWrite(RECORD_BUTTON_PIN, HIGH);
 handleRecordButton();
 ASSERT_FALSE(recording);
}

TEST_F(RecordControlTest, startRecording_SetsRecordingStateAndResetsCounters) {
 ASSERT_FALSE(recording);
 ASSERT_EQ(recStepCount, 0);

 startRecording();

 ASSERT_TRUE(recording);
 ASSERT_EQ(recStepCount, 0);
 ASSERT_EQ(recChordNumKeys, 0);
}

TEST_F(RecordControlTest, startRecording_CalledTwiceDoesNotReset) {
 startRecording();
 ASSERT_TRUE(recording);

 recStepCount = 5;
 recChordNumKeys = 3;

 startRecording();

 ASSERT_TRUE(recording);
 ASSERT_EQ(recStepCount, 5);
 ASSERT_EQ(recChordNumKeys, 3);
}

TEST_F(RecordControlTest, stopRecording_WithPendingChordCommitsFirst) {
 recording = true;
 recChordNumKeys = 2;
 recChordKeys[0] = 5;
 recChordKeys[1] = 7;
 recChordStartTime = millis();

 stopRecording();

 ASSERT_GT(currentSequence.length, 0);
 ASSERT_FALSE(recording);
 ASSERT_GE(recStepCount, 1);
}

TEST_F(RecordControlTest, stopRecording_NoNotesRecorded) {
 recording = true;
 recStepCount = 0;
 recChordNumKeys = 0;

 stopRecording();

 ASSERT_FALSE(recording);
 ASSERT_EQ(recStepCount, 0);
 ASSERT_EQ(currentSequence.length, 0);
}

TEST_F(RecordControlTest, stopRecording_CommitsMultipleChords) {
 recording = true;
 recStepCount = 3;
 recChordNumKeys = 1;
 recChordKeys[0] = 12;

 stopRecording();

 ASSERT_GT(currentSequence.length, 0);
 ASSERT_FALSE(recording);
}

TEST_F(RecordControlTest, recordKeyPress_StartsNewChord) {
 recording = true;
 recChordNumKeys = 0;

 mock_millis_set_time(1000);
 recordKeyPress(5);

 ASSERT_EQ(recChordNumKeys, 1);
 ASSERT_EQ(recChordKeys[0], 5);
 ASSERT_EQ(recChordStartTime, 1000);
}

TEST_F(RecordControlTest, recordKeyPress_AddsToExistingChordWithinWindow) {
 recording = true;
 recChordStartTime = 1000;
 recChordNumKeys = 1;
 recChordKeys[0] = 5;

 mock_millis_set_time(1100);
 recordKeyPress(7);

 ASSERT_EQ(recChordNumKeys, 2);
 ASSERT_EQ(recChordKeys[0], 5);
 ASSERT_EQ(recChordKeys[1], 7);
}

TEST_F(RecordControlTest, recordKeyPress_CommitsOldChordWhenPastWindow) {
 recording = true;
 recChordStartTime = 1000;
 recChordNumKeys = 1;
 recChordKeys[0] = 5;
 recStepCount = 0;

 mock_millis_set_time(1300);

 recordKeyPress(7);

 ASSERT_EQ(recStepCount, 1);
 ASSERT_EQ(recChordNumKeys, 1);
 ASSERT_EQ(recChordKeys[0], 7);
}

TEST_F(RecordControlTest, recordKeyPress_HonorsMaxKeysPerStep) {
 recording = true;
 recChordStartTime = 1000;
 recChordNumKeys = MAX_KEYS_PER_STEP;
 ASSERT_EQ(recChordNumKeys, 4);

 mock_millis_set_time(1100);
 recordKeyPress(9);

 ASSERT_EQ(recStepCount, 1);
 ASSERT_EQ(recChordNumKeys, 1);
 ASSERT_EQ(recChordKeys[0], 9);
}

TEST_F(RecordControlTest, recordKeyRelease_CommitsOnFirstValidRelease) {
 recording = true;
 recChordNumKeys = 2;
 recChordKeys[0] = 5;
 recChordKeys[1] = 7;
 recChordStartTime = millis();
 recStepCount = 0;

 recordKeyRelease(5);

 ASSERT_EQ(recStepCount, 1);
 ASSERT_EQ(recChordNumKeys, 0);
}

TEST_F(RecordControlTest, recordKeyRelease_IgnoresIfNoChordInProgress) {
 recording = true;
 recChordNumKeys = 0;

 recordKeyRelease(5);

 ASSERT_EQ(recStepCount, 0);
 ASSERT_EQ(recChordNumKeys, 0);
}

TEST_F(RecordControlTest, recordKeyRelease_IgnoresKeyNotInChord) {
 recording = true;
 recChordNumKeys = 2;
 recChordKeys[0] = 5;
 recChordKeys[1] = 7;

 recordKeyRelease(9);

 ASSERT_EQ(recStepCount, 0);
 ASSERT_EQ(recChordNumKeys, 2);
}

TEST_F(RecordControlTest, commitRecordedStep_WritesStepToSequence) {
 recording = true;
 recChordNumKeys = 2;
 recChordKeys[0] = 5;
 recChordKeys[1] = 7;
 recChordStartTime = 1000;
 recStepCount = 0;

 mock_millis_set_time(1500);
 commitRecordedStep();

 ASSERT_EQ(recStepCount, 1);
 ASSERT_EQ(currentSequence.steps[0].numKeys, 2);
 ASSERT_EQ(currentSequence.steps[0].keys[0], 5);
 ASSERT_EQ(currentSequence.steps[0].keys[1], 7);
 ASSERT_EQ(currentSequence.steps[0].duration, 500);
}

TEST_F(RecordControlTest, commitRecordedStep_HandlesMinDuration) {
 recording = true;
 recChordNumKeys = 1;
 recChordKeys[0] = 5;
 recChordStartTime = 1000;
 recStepCount = 0;

 mock_millis_set_time(1005);
 commitRecordedStep();

 ASSERT_EQ(currentSequence.steps[0].duration, MIN_NOTE_DURATION);
}

TEST_F(RecordControlTest, commitRecordedStep_HandlesMaxDuration) {
 recording = true;
 recChordNumKeys = 1;
 recChordKeys[0] = 5;
 recChordStartTime = 1000;
 recStepCount = 0;

 mock_millis_set_time(7000);
 commitRecordedStep();

 ASSERT_EQ(currentSequence.steps[0].duration, MAX_NOTE_DURATION);
}

TEST_F(RecordControlTest, commitRecordedStep_ResetsChordState) {
 recording = true;
 recChordNumKeys = 2;
 recChordKeys[0] = 5;
 recChordKeys[1] = 7;
 recStepCount = 0;

 mock_millis_set_time(1500);
 commitRecordedStep();

 ASSERT_EQ(recChordNumKeys, 0);
 ASSERT_EQ(recStepCount, 1);
}

TEST_F(RecordControlTest, commitRecordedStep_IgnoresEmptyChord) {
 recording = true;
 recChordNumKeys = 0;
 recStepCount = 0;

 commitRecordedStep();

 ASSERT_EQ(recStepCount, 0);
}

TEST_F(RecordControlTest, flashWhiteAnimation_CalledInStartRecording) {
 recording = false;
 startRecording();
 ASSERT_TRUE(recording);
}

TEST_F(RecordControlTest, flashWhiteAnimation_CalledInStopRecording) {
 recording = true;
 recStepCount = 1;
 stopRecording();
 ASSERT_FALSE(recording);
}

} // namespace
