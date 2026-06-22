#ifndef SLAVE_CONTROL_TESTABLE_H
#define SLAVE_CONTROL_TESTABLE_H

#include "firmware_types.h"

void demoteToSlave();
void configureNotes();

extern const int baseNoteFreqs[NUM_KEYS];

#endif // SLAVE_CONTROL_TESTABLE_H
