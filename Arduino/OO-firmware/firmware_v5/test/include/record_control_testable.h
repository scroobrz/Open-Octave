#ifndef RECORD_CONTROL_TESTABLE_H
#define RECORD_CONTROL_TESTABLE_H

// Testable version of record_control.ino functions
// Extract these functions from record_control.ino for unit testing

void handleRecordButton();
void startRecording();
void stopRecording();
void recordKeyPress(int globalKey);
void recordKeyRelease(int globalKey);
void commitRecordedStep();
void flashWhiteAnimation();

#endif // RECORD_CONTROL_TESTABLE_H
