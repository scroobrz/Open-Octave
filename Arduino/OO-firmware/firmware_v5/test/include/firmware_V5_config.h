#ifndef FIRMWARE_V5_CONFIG_H
#define FIRMWARE_V5_CONFIG_H

#include <cstdint>

// Key and hardware configuration
#define NUM_KEYS 12
#define MAX_TOTAL_KEYS 48  // Support up to 4 chained modules

// Timing constants
#define BUTTON_DEBOUNCE_DELAY 50

// Timing for auto modes
#define MIN_NOTE_DURATION 100
#define MAX_NOTE_DURATION 5000
#define CHORD_WINDOW_MS 200
#define MAX_KEYS_PER_STEP 4
#define MAX_SEQUENCE_LENGTH 100

// Servo timing
#define SERVO_FREQ 50
#define SERVO_RELEASE_DELAY 50

// LED settings
#define LED_BRIGHTNESS 255
#define RECORD_FLASH_HOLD 200  // Duration of recording LED flash in milliseconds

// Hardware pins
#define SPEAKER_PIN 25
#define LED_STRIP_PIN 26

// PCA9555 configuration
#define RECORD_BUTTON_PIN 0
#define ON_OFF_PIN 1
#define GUIDED_SEQ_BUTTON_PIN 2
#define TEACHING_SEQ_BUTTON_PIN 3
#define KEY0_BUTTON_PIN 4
#define KEY1_BUTTON_PIN 5
#define KEY2_BUTTON_PIN 6
#define KEY3_BUTTON_PIN 7
#define GIPO_UNUSED 8

// Other button pins
#define KEY4_BUTTON_PIN 9
#define KEY5_BUTTON_PIN 10
#define KEY6_BUTTON_PIN 11
#define KEY7_BUTTON_PIN 12
#define KEY8_BUTTON_PIN 13
#define KEY9_BUTTON_PIN 14
#define KEY10_BUTTON_PIN 15

// Additional pins for remaining
#define KEY11_BUTTON_PIN 16
#define KEY12_BUTTON_PIN 17
#define KEY13_BUTTON_PIN 18
#define KEY14_BUTTON_PIN 19
#define KEY15_BUTTON_PIN 20

// Serial communication
#define SERIAL_BUF_SIZE 256
#define RX1 18
#define TX1 17
#define RX2 16
#define TX2 4

// Test configuration flags
#ifdef TESTING
    // Test-specific constants when running native tests
    #define TEST_MILLIS_START 0
#else
    // Normal embedded configuration
#endif

// Forward declarations for external libraries (mocked)
class PCA9685;
class PCA9555;
class ServoDriver;

#endif // FIRMWARE_V5_CONFIG_H
