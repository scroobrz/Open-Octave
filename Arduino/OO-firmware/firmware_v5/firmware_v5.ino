/*
 * FIRMWARE V5
 *
 * Each module is a 12-key octave with servo actuators, per-key LEDs, and a
 * speaker. Modules daisy-chain together via serial to form larger keyboards,
 * with automatic master/slave role assignment through a heartbeat protocol.
 *
 * Sequence playback operates in one of two modes:
 *   - GUIDED:  LEDs light up sequentially; the user must press and hold the
 *              correct key for the step's duration before the sequence advances.
 *   - TEACHING: LEDs light up and servos auto-press keys to demonstrate melodies.
 *
 * Sound is always triggered by button presses — whether the user presses a key
 * or a servo pulls it down, the button underneath is what triggers the sound.
 *
 * NETWORKING:
 *   The master module connects as a WiFi client to the controller's network and
 *   opens a WebSocket connection to the controller server. Commands and log
 *   output flow over this link as a "serial-over-WiFi" transport. Slave modules
 *   do not use WiFi; they receive commands from the master via the daisy-chain.
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
#include <WebSocketsClient.h>
#include <cstdint>

// ============ HARDWARE DEFINITIONS ============

Key keys[NUM_KEYS] = {
    {KEY0_BUTTON_PIN, KEY0_LED_INDEX, KEY0_SERVO_CHANNEL, KEY0_NOTE, false}, // C4
    {KEY1_BUTTON_PIN, KEY1_LED_INDEX, KEY1_SERVO_CHANNEL, KEY1_NOTE, false}, // C#4
    {KEY2_BUTTON_PIN, KEY2_LED_INDEX, KEY2_SERVO_CHANNEL, KEY2_NOTE, false}, // D4
    {KEY3_BUTTON_PIN, KEY3_LED_INDEX, KEY3_SERVO_CHANNEL, KEY3_NOTE, false}, // D#4
    {KEY4_BUTTON_PIN, KEY4_LED_INDEX, KEY4_SERVO_CHANNEL, KEY4_NOTE, false}, // E4
    {KEY5_BUTTON_PIN, KEY5_LED_INDEX, KEY5_SERVO_CHANNEL, KEY5_NOTE, false}, // F4
    {KEY6_BUTTON_PIN, KEY6_LED_INDEX, KEY6_SERVO_CHANNEL, KEY6_NOTE, false}, // F#4
    {KEY7_BUTTON_PIN, KEY7_LED_INDEX, KEY7_SERVO_CHANNEL, KEY7_NOTE, false}, // G4
    {KEY8_BUTTON_PIN, KEY8_LED_INDEX, KEY8_SERVO_CHANNEL, KEY8_NOTE, false}, // G#4
    {KEY9_BUTTON_PIN, KEY9_LED_INDEX, KEY9_SERVO_CHANNEL, KEY9_NOTE, false}, // A4
    {KEY10_BUTTON_PIN, KEY10_LED_INDEX, KEY10_SERVO_CHANNEL, KEY10_NOTE, false}, // A#4
    {KEY11_BUTTON_PIN, KEY11_LED_INDEX, KEY11_SERVO_CHANNEL, KEY11_NOTE, false}  // B4
};

ServoDriver servoDriver;
PCA9555 ioport(0x20);

Adafruit_NeoPixel leds(NUM_KEYS, LED_STRIP_PIN, NEO_GRB + NEO_KHZ800);

WebSocketsClient webSocket;

HardwareSerial UpstreamSerial(1);
HardwareSerial DownstreamSerial(2);

// ============ GLOBAL STATE ============

bool on = true;
unsigned long lastOnOffSwitchTime = 0;

bool isMaster = true;
uint8_t moduleChainIndex = 0;
uint8_t numModulesInChain = 1;

unsigned long timeLastHeartbeatSent = 0;
unsigned long timeLastHeartbeatReceived = 0;
unsigned long timeLastHeartbeatReplyReceived = 0;

bool uploadingSequence = false;
uint8_t uploadStepCount = 0;
Sequence uploadSequenceBuffer;

SequenceMode currentSequenceMode;
Sequence currentSequence;
bool sequenceRunning = false;
int currentSequenceStepIndex = 0;
unsigned long currentStepStartTime = 0;
unsigned long lastSequenceButtonPressTime = 0;
#define CURRENT_STEP currentSequence.steps[currentSequenceStepIndex]
#define PREVIOUS_STEP currentSequence.steps[currentSequenceStepIndex - 1]

// To keep track of key presses across other chained modules for the 
// master to handle sequences
bool globalKeyIsPressed[MAX_TOTAL_KEYS] = {false};
unsigned long globalKeyPressTime[MAX_TOTAL_KEYS] = {0};
unsigned long toneStartTime[MAX_TOTAL_KEYS] = {0};

bool waitingForServoRelease = false;
unsigned long servoReleaseStartTime = 0;

bool recording = false;
uint8_t recStepCount = 0;
uint8_t recChordKeys[MAX_KEYS_PER_STEP];
uint8_t recChordNumKeys = 0;
unsigned long recChordStartTime = 0;

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
bool wsReady = false;  // Prevents wsSendLog() from running before webSocket.begin()

char serialBuf[SERIAL_BUF_SIZE];
uint8_t serialBufPos = 0;
bool serialBufOverflow = false;

char upstreamSerialBuf[SERIAL_BUF_SIZE];
uint8_t upstreamSerialBufPos = 0;
bool upstreamSerialBufOverflow = false;

char downstreamSerialBuf[SERIAL_BUF_SIZE];
uint8_t downstreamSerialBufPos = 0;
bool downstreamSerialBufOverflow = false;

/*
===============================
     CORE ARDUINO FUNCTIONS
===============================
These are the two functions that Arduino calls automatically.
*/

// runs once
void setup() {
  bool hasUpstream = checkUpstream(); // raw GPIO read before serial init

  // === SERIAL INITIALIZATION ===
  Serial.begin(115200);
  UpstreamSerial.begin(115200, SERIAL_8N1, RX1, TX1);
  DownstreamSerial.begin(115200, SERIAL_8N1, RX2, TX2);

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

  LOG("[SETUP] Initializing LEDs... ");
  leds.begin();
  leds.setBrightness(LED_BRIGHTNESS);
  leds.show();
  LOGF("OK (%d LEDs initialized; brightness: %d)\n", NUM_KEYS, LED_BRIGHTNESS);

  // initialize each key
  LOGLN("[SETUP] Initializing keys:");
  for (int i = 0; i < NUM_KEYS; i++) {
    ioport.pinMode(keys[i].buttonPin, INPUT);
    servoRest(keys[i].servoChannel);
    keys[i].isPressed = false;

    LOGF("  Key %d: btn_pin=%d, led_idx=%d, servo_ch=%d, freq=%dHz\n", i,
         keys[i].buttonPin, keys[i].ledIndex, keys[i].servoChannel,
         keys[i].noteFreq);
  }
  LOGF("OK (%d keys initialized)\n", NUM_KEYS);

  LOGF("[SETUP] Configuring external button pins... ");
  ioport.pinMode(ON_OFF_PIN, INPUT);
  ioport.pinMode(GUIDED_SEQ_BUTTON_PIN, INPUT);
  ioport.pinMode(TEACHING_SEQ_BUTTON_PIN, INPUT);
  LOGLN("OK");

  if (!hasUpstream){
    LOGLN("[SETUP] Connecting to WiFi...");
    connectToWifi();
    connectToWebsocket();
    LOGLN("[SETUP] WiFi & WebSocket Active!");
  }

  LOGLN("========================================");
  LOGLN("[SETUP] Complete!");
  LOGLN("========================================\n");
}

// runs repeatedly forever
void loop() {
  ioport.pinStates();
  checkOnOff();

  if (on){
    handleChainCommunication();

    if (isMaster){
      handleControllerCommunication();
      checkWifiStatus();
      handleSequencePlayback();
      handleSequenceButtons();
      handleRecordButton();
    }

    handleKeyPresses();
  }
}

void checkOnOff(){
  if (millis() - lastOnOffSwitchTime >= BUTTON_DEBOUNCE_DELAY){
    bool onSwitch = ioport.stateOfPin(ON_OFF_PIN);

    if (on != onSwitch){
      if (onSwitch) {
        on = true;
        playStartupAnimation();
      } else {
        if (recording) stopRecording();
        stopSequence();
        playShutdownAnimation();
        on = false;
      }
      lastOnOffSwitchTime = millis();
    }
  }
}
