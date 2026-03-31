#ifndef GLOBALS_H
#define GLOBALS_H

#include "firmware_types.h"
#include <memory>

// Global state variables
extern bool on;
extern unsigned long lastOnOffSwitchTime;

extern bool isMaster;
extern uint8_t moduleChainIndex;
extern uint8_t numModulesInChain;

// Key array (defined in firmware)
extern struct Key keys[NUM_KEYS];

extern unsigned long timeLastHeartbeatSent;
extern unsigned long timeLastHeartbeatReceived;
extern unsigned long timeLastHeartbeatReplyReceived;

extern bool uploadingSequence;
extern uint8_t uploadStepCount;
extern Sequence uploadSequenceBuffer;

extern SequenceMode currentSequenceMode;
extern Sequence currentSequence;
extern bool sequenceRunning;
extern int currentSequenceStepIndex;
extern unsigned long currentStepStartTime;
extern unsigned long lastSequenceButtonPressTime;

extern bool globalKeyIsPressed[MAX_TOTAL_KEYS];
extern unsigned long globalKeyPressTime[MAX_TOTAL_KEYS];
extern unsigned long toneStartTime[MAX_TOTAL_KEYS];

extern bool waitingForServoRelease;
extern unsigned long servoReleaseStartTime;

extern bool recording;
extern uint8_t recStepCount;
extern uint8_t recChordKeys[MAX_KEYS_PER_STEP];
extern uint8_t recChordNumKeys;
extern unsigned long recChordStartTime;
extern bool lastRecordButtonState;

extern bool testLogEnabled;
extern uint16_t testLogRunId;
extern uint16_t testLogEventId;
extern int8_t testLogLastManualKey;
extern unsigned long testLogLastManualTime;
extern uint8_t testLogManualRepeatStreak;
extern unsigned long testLogExpectedNextStepStartTime;
extern int8_t testLogLastAutoKey;
extern uint8_t testLogAutoRepeatStreak;

extern unsigned long lastWifiCheckTime;
extern bool isWifiConnected;
extern bool wsReady;

extern char serialBuf[SERIAL_BUF_SIZE];
extern uint8_t serialBufPos;
extern bool serialBufOverflow;

extern char upstreamSerialBuf[SERIAL_BUF_SIZE];
extern uint8_t upstreamSerialBufPos;
extern bool upstreamSerialBufOverflow;

extern char downstreamSerialBuf[SERIAL_BUF_SIZE];
extern uint8_t downstreamSerialBufPos;
extern bool downstreamSerialBufOverflow;

// Function prototypes that need global access
extern void recordKeyPress(int globalKey);
extern void recordKeyRelease(int globalKey);
extern void commitRecordedStep();
extern void startSequence(SequenceMode mode);
extern void stopSequence();
extern void handleSequencePlayback();
extern void handleTeachingModePlayback();
extern void handleGuidedModePlayback();
extern void executeCurrentSequenceStep();
extern void resetKey(int keyIndex);
extern void evaluateWrongKeyFeedback(int globalKey, bool isPressed);
extern void flashWhiteAnimation();

// Test helper functions
void reset_globals();
void setup_test_recording_state();

#endif // GLOBALS_H
