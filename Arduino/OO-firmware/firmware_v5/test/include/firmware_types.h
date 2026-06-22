#ifndef FIRMWARE_TYPES_H
#define FIRMWARE_TYPES_H

#include <cstdint>
#include <cstring>
#include "../include/firmware_V5_config.h"

// Forward declare external dependencies
class Adafruit_NeoPixel;
class HardwareSerial;

// Color constants (32-bit RGB values) — must match firmware_V5_config.h
#define COLOR_RED       0xFF0000
#define COLOR_ORANGE    0xFF8000
#define COLOR_YELLOW    0xFFFF00
#define COLOR_GREEN     0x00FF00
#define COLOR_BLUE      0x0000FF
#define COLOR_INDIGO    0x4B0082
#define COLOR_VIOLET    0x8000FF
#define COLOR_WHITE     0xFFFFFF
#define COLOR_MAGENTA   0xFF00FF
#define COLOR_PINK      0xFF69B4

// Sequence modes
enum SequenceMode {
    GUIDED = 0,
    TEACHING = 1
};

// Audio synthesis modes
enum SynthMode {
    SYNTH_ADDITIVE = 0,
    SYNTH_KARPLUS_STRONG = 1
};

// Key structure for one physical key
struct Key {
    uint8_t buttonPin;    // PCA9555 pin for button
    uint8_t ledIndex;   // NeoPixel LED index
    uint8_t servoChannel; // PCA9685 servo channel
    uint16_t noteFreq;  // MIDI note frequency
    bool isPressed;     // Current press state
};

// One step in a sequence (can be a chord or single note)
struct SequenceStep {
    uint8_t numKeys;                              // Number of keys in this step (1-MAX_KEYS_PER_STEP)
    uint8_t keys[MAX_KEYS_PER_STEP];              // Global key indices (0-47: max across 4 modules)
    uint32_t colors[MAX_KEYS_PER_STEP];          // LED colors for each key
    uint16_t duration;                            // Duration in milliseconds (100-5000)
};

// Complete sequence with metadata
struct Sequence {
    uint16_t id;                                  // Sequence ID
    char name[32];                               // Human-readable name
    uint16_t length;                              // Number of steps
    SequenceStep steps[MAX_SEQUENCE_LENGTH];     // Array of steps
};

// External dependencies (will be mocked in tests)
extern Adafruit_NeoPixel leds;
extern PCA9555 ioport;
extern ServoDriver servoDriver;
extern HardwareSerial UpstreamSerial;
extern HardwareSerial DownstreamSerial;

// External functions (prototypes)
bool isValidLocalKeyIndex(int keyIndex);
bool isValidGlobalKeyIndex(int keyIndex);
void resetKey(int keyIndex);
void lightUpKey(int keyIndex, uint32_t color);
void lightDownKey(int keyIndex);
void autoPressKey(int keyIndex);
void autoReleaseKey(int keyIndex);
void flashWhiteAnimation();
void testLogLogError(uint8_t errorCode, const char* eventType);
void startSequence(SequenceMode mode);
void stopSequence();
void executeCurrentSequenceStep();
void handleSequencePlayback();
void evaluateWrongKeyFeedback(int globalKey, bool isPressed);
const char* getCurrentSequenceModeString();
void chainSendKeyCmd(HardwareSerial& serial, char cmd, int globalKey);
void chainSendKeyCmdWithColor(HardwareSerial& serial, char cmd, int globalKey, uint32_t color);

#endif // FIRMWARE_TYPES_H
