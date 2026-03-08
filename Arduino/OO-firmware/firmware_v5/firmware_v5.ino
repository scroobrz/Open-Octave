/*
 * FIRMWARE V5
 *
 * This firmware currently controls 3 keys across the following three modes:
 *   - MANUAL: User plays keys manually, no automation
 *   - GUIDED: Guided mode - LEDs light up, user must press key to advance
 *   - TEACHING: LEDs + servos play automatically (no user input needed)
 *
 * Sound is always triggered by button presses - whether the user presses a key
 * or a servo pulls it down, the button underneath is what triggers the sound.
 *
 * NETWORKING:
 *   The ESP32 hosts a WiFi Access Point and runs a WebSocket server on port 81.
 *   Commands are received as single-character text messages (same format as USB
 *   serial commands), and all log output is broadcast back to connected clients.
 *   This "serial-over-WebSocket" design keeps the firmware simple and makes the
 *   WiFi interface behave identically to the USB serial interface. Sequences are
 *   uploaded through a specialised protocol; these are the only commands
 *   that are not single-character.
 */

#include "Arduino.h"
#include "HardwareSerial.h"
#include "PCA9685.h"
#include "clsPCA9555.h"
#include "firmware_V5_config.h"
#include "firmware_V5_debug.h"
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <cstdint>

// ============ HARDWARE DEFINITIONS ============

Key keys[NUM_KEYS] = {
    {KEY0_BUTTON_PIN, KEY0_LED_PIN, nullptr, KEY0_SERVO_CHANNEL, KEY0_NOTE, false}, // C4
    {KEY1_BUTTON_PIN, KEY1_LED_PIN, nullptr, KEY1_SERVO_CHANNEL, KEY1_NOTE, false}, // C#4
    {KEY2_BUTTON_PIN, KEY2_LED_PIN, nullptr, KEY2_SERVO_CHANNEL, KEY2_NOTE, false}, // D4
    {KEY3_BUTTON_PIN, KEY3_LED_PIN, nullptr, KEY3_SERVO_CHANNEL, KEY3_NOTE, false}, // D#4
    {KEY4_BUTTON_PIN, KEY4_LED_PIN, nullptr, KEY4_SERVO_CHANNEL, KEY4_NOTE, false}, // E4
    {KEY5_BUTTON_PIN, KEY5_LED_PIN, nullptr, KEY5_SERVO_CHANNEL, KEY5_NOTE, false}, // F4
    {KEY6_BUTTON_PIN, KEY6_LED_PIN, nullptr, KEY6_SERVO_CHANNEL, KEY6_NOTE, false}, // F#4
    {KEY7_BUTTON_PIN, KEY7_LED_PIN, nullptr, KEY7_SERVO_CHANNEL, KEY7_NOTE, false}, // G4
    {KEY8_BUTTON_PIN, KEY8_LED_PIN, nullptr, KEY8_SERVO_CHANNEL, KEY8_NOTE, false}, // G#4
    {KEY9_BUTTON_PIN, KEY9_LED_PIN, nullptr, KEY9_SERVO_CHANNEL, KEY9_NOTE, false}, // A4
    {KEY10_BUTTON_PIN, KEY10_LED_PIN, nullptr, KEY10_SERVO_CHANNEL, KEY10_NOTE, false}, // A#4
    {KEY11_BUTTON_PIN, KEY11_LED_PIN, nullptr, KEY11_SERVO_CHANNEL, KEY11_NOTE, false}  // B4
};

ServoDriver servoDriver;
PCA9555 ioport(0x20);

WebSocketsServer webSocket(81);

// ============ GLOBAL STATE ============

bool uploadingSequence = false;
uint8_t uploadStepCount = 0;
Sequence uploadSequenceBuffer;

SequenceMode currentSequenceMode;
Sequence currentSequence;
bool sequenceRunning = false;
int currentSequenceStepIndex = 0;
unsigned long currentStepStartTime = 0;

unsigned long lastKeyPressTime[NUM_KEYS] = {0};
unsigned long toneStartTime[NUM_KEYS] = {0};

bool waitingForServoRelease = false;
unsigned long servoReleaseStartTime = 0;

bool testLogEnabled = false;
uint16_t testLogRunId = 0;
uint16_t testLogEventId = 0;
int8_t testLogLastManualKey = -1;
unsigned long testLogLastManualTime = 0;
uint8_t testLogManualRepeatStreak = 0;
unsigned long testLogExpectedNextStepStartTime = 0;
int8_t testLogLastAutoKey = -1;
uint8_t testLogAutoRepeatStreak = 0;

unsigned long lastWifiCheckTime = 0;
bool isWifiConnected = false;
bool wsReady = false;  // Prevents wsBroadcastLog() from running before webSocket.begin()

char serialBuf[SERIAL_BUF_SIZE];
uint8_t serialBufPos = 0;
bool serialBufOverflow = false;  // true = discard bytes until next newline

// Tracks the most recently pressed key index in the current loop (or -1 if none)
int keyJustPressed = -1;

/*
===============================
     CORE ARDUINO FUNCTIONS
===============================
These are the two functions that Arduino calls automatically.
*/

// runs once
void setup() {
  LOGLN("\n========================================");
  LOGLN("    OPEN OCTAVE FIRMWARE V5 - INIT");
  LOGLN("========================================");

  // ===== VALIDATION =====
  LOG("[SETUP] Validating hardware config... ");
  if (!validateHardwareInit()) {
    LOGLN("[ERROR] CRITICAL: Hardware validation failed!");
    LOGLN("System halted. Check configuration.");
    while (true) {
      delay(1000);
    } // Halt - configuration error
  }
  LOGLN("OK");

  LOGLN("[SETUP] Loading default sequence... ");
  loadDefaultSequence();
  LOGF("OK (\"%s\", %d steps)\n", currentSequence.name, currentSequence.length);

  // ===== INITIALIZATION =====
  LOG("[SETUP] Configuring speaker... ");
  pinMode(SPEAKER_PIN, OUTPUT);
  noTone(SPEAKER_PIN);
  LOGF("OK (speaker_pin: %d)\n", SPEAKER_PIN);

  LOG("[SETUP] Initializing I2C... ");
  Wire.begin(21, 22);
  LOGLN("OK");

  LOG("[SETUP] Initializing expansion board... ");
  if (!ioport.begin()) {
    LOGLN("[ERROR] PCA9555 I/O expander not responding at address 0x20!");
    LOGLN("Check I2C wiring (SDA=21, SCL=22) and verify the chip is powered.");
    while (true) { delay(1000); }  // Halt - buttons won't work without it
  }
  LOGLN("OK");

  LOG("[SETUP] Initializing servo driver... ");
  servoDriver.init();
  servoDriver.setFrequency(SERVO_FREQ);
  LOGF("OK (freq: %dHz)\n", SERVO_FREQ);

  // initialize each key
  LOGLN("[SETUP] Initializing keys:");
  for (int i = 0; i < NUM_KEYS; i++) {
    ioport.pinMode(keys[i].buttonPin, INPUT);
    servoRest(keys[i].servoChannel);

    // Create NeoPixel object dynamically (can't be done at global scope)
    keys[i].led = new Adafruit_NeoPixel(LEDS_PER_KEY, keys[i].ledPin, NEO_GRB + NEO_KHZ800);
    keys[i].led->begin();
    keys[i].led->setBrightness(LED_BRIGHTNESS);
    keys[i].led->show();
    keys[i].isPressed = false;

    LOGF("  Key %d: btn_pin=%d, led_pin=%d, servo_ch=%d, freq=%dHz\n", i,
         keys[i].buttonPin, keys[i].ledPin, keys[i].servoChannel,
         keys[i].noteFreq);
  }
  LOGF("OK (%d keys initialized)\n", NUM_KEYS);

  LOGLN("[SETUP] Connecting to WiFi...");
  setupWiFi();
  LOGLN("[SETUP] WiFi & WebSocket Active!");

  LOGLN("========================================");
  LOGLN("[SETUP] Complete!");
  LOGLN("========================================\n");
}

// runs repeatedly forever
void loop() {
  webSocket.loop();        // handle WebSocket events
  handleSerialCommand();   // handle serial commands
  handleKeyPresses();      // detect any key presses and play sounds
  checkWiFiStatus();       // check wifi connection state

  // if we're in an automatic mode, handle the sequence playback
  if (currentSequenceMode != MANUAL) {
    handleAutomaticModes();
  }
}
