#ifndef HELPERS_TESTABLE_H
#define HELPERS_TESTABLE_H

#include <cstdint>

bool checkUpstream();
uint8_t getKeyIndex(uint8_t position, uint8_t row);
uint8_t getKeyPosition(uint8_t keyIndex);
uint16_t getKeyFrequency(uint8_t keyIndex);
const char* getColorString(uint32_t color);
void toLowercase(char &c);
bool isValidGlobalKeyIndex(int keyIndex);

#endif // HELPERS_TESTABLE_H
