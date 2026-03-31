#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include "globals.h"
#include "test_debug.h"

// Inline helper functions to avoid duplicate definitions
inline const char* getCurrentSequenceModeString() {
    if (currentSequenceMode == GUIDED) return "GUIDED";
    if (currentSequenceMode == TEACHING) return "TEACHING";
    return "UNKNOWN";
}

inline void emitStatus() {
    LOGF("STATUS running=%d seq=%d step=%d mode=%s\n",
         sequenceRunning, currentSequence.id,
         (sequenceRunning ? currentSequenceStepIndex : -1),
         (sequenceRunning ? getCurrentSequenceModeString() : "N/A"));
}

#endif // TEST_UTILS_H
