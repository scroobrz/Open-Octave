#ifndef VALIDATION_TESTABLE_H
#define VALIDATION_TESTABLE_H

bool validateHardwareInit();
bool isValidLocalKeyIndex(int keyIndex);
bool isValidGlobalKeyIndex(int keyIndex);
void testLEDs();
void testServos();

#endif // VALIDATION_TESTABLE_H
