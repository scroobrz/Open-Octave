/*
 * FIRMWARE V2
 */

#include "PCA9685.h" // controls the Servo Motor driver
#include <Adafruit_NeoPixel.h> // controls the LED stick/strip
#include <Wire.h>              // allows communication with the Servo driver

#define SPEAKER_PIN 2
#define MODE_SWITCH_PIN 3

#define SERVO_FREQ 50
#define SERVO_REST_ANGLE 0
#define SERVO_PRESS_ANGLE 90

#define NUM_LEDS 10
#define NUM_KEYS 3

#define KEY0_BUTTON_PIN 4
#define KEY0_LED_PIN 5
#define KEY0_SERVO_CHANNEL 0
#define KEY0_NOTE 262

#define KEY1_BUTTON_PIN 6
#define KEY1_LED_PIN 7
#define KEY1_SERVO_CHANNEL 1
#define KEY1_NOTE 294

#define KEY2_BUTTON_PIN 8
#define KEY2_LED_PIN 9
#define KEY2_SERVO_CHANNEL 2
#define KEY2_NOTE 330

enum Mode {
  MANUAL,         // no automatic functions
  AUTOMATIC_LEDS, // system plays LED sequences automatically
  FULL_AUTOMATIC  // system plays LED and servo sequences automatically
};

struct Key {
  int buttonPin;              // digital pin for the button
  Adafruit_NeoPixel ledStick; // controller for the LED stick
  int servoChannel;           // channel on PCA9685 servo driver (0-15)
  int noteFreq;               // sound frequency in Hz
  bool isPressed;
};

#define LED_STICK(pin) Adafruit_NeoPixel(NUM_LEDS, pin, NEO_GRB + NEO_KHZ800)
Key keys[NUM_KEYS] = {
    {KEY0_BUTTON_PIN, LED_STICK(KEY0_LED_PIN), KEY0_SERVO_CHANNEL, KEY0_NOTE, false}, // C4
    {KEY1_BUTTON_PIN, LED_STICK(KEY1_LED_PIN), KEY1_SERVO_CHANNEL, KEY1_NOTE, false}, // D4
    {KEY2_BUTTON_PIN, LED_STICK(KEY2_LED_PIN), KEY2_SERVO_CHANNEL, KEY2_NOTE, false}  // E4
};

ServoDriver servoDriver;
Mode currentMode = MANUAL;

void setup() {
  pinMode(MODE_SWITCH_PIN, INPUT);
  pinMode(SPEAKER_PIN, OUTPUT);

  Wire.begin(); // start the I2C communication connection
  servoDriver.init();

  for (int i = 0; i < NUM_KEYS; i++) {
    pinMode(keys[i].buttonPin, INPUT);
    keys[i].ledStick.begin();
    keys[i].ledStick.show();
    keys[i].isPressed = false;
    servoDriver.setAngle(keys[i].servoChannel, SERVO_REST_ANGLE);
  }
}

// runs repeatedly forever.
void loop() {
  if (digitalRead(BUTTON_PIN) == HIGH) { // button being pressed

    servoDriver.setAngle(0, 90); // move servo on channel 0 to 90 degrees

    for (int i = 0; i < NUM_LEDS; i++) {
      // makes a rainbow on the LED
      int hue = i * (65536 / NUM_LEDS);
      strip.setPixelColor(i, strip.ColorHSV(hue, 255, 255));
    }
    strip.show();

    tone(SPEAKER_PIN, 262, 500); // play 262Hz (middle C) for 500 milliseconds
                                 // (doesn't sound like middle C lol)

  } else {
    // button not being pressed; reset everything
    servoDriver.setAngle(0, 0);

    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(0, 0, 0));
    }
    strip.show();

    noTone(SPEAKER_PIN);
  }
}