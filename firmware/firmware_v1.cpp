/*
 * FIRMWARE V1
 * This program controls a servo motor, LED strip, and speaker based on a button
 * press.
 */

#include "PCA9685.h"           // controls the Servo Motor driver
#include <Adafruit_NeoPixel.h> // controls the LED stick/strip
#include <Wire.h>              // allows communication with the Servo driver

// physical pin definitions
const int BUTTON_PIN = 2;  // button is plugged into D2
const int SPEAKER_PIN = 3; // speaker is plugged into D3
const int LED_PIN = 6;     // LED stick is plugged into D6

// config
const int NUM_LEDS = 10;   // number of LED lights on the stick
const int SERVO_FREQ = 50; // servo motors typically run at 50Hz

// initializers
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800); // controller for the LED strip
ServoDriver servo; // controller for the servo

// runs once when the Arduino is turned on or reset
void setup() {
  pinMode(BUTTON_PIN, INPUT);
  pinMode(SPEAKER_PIN, OUTPUT);

  strip.begin();
  strip.show();

  Wire.begin(); // start the I2C communication connection
  servo.init();
  servo.setAngle(0, 0);
}

// runs repeatedly forever.
void loop() {
  if (digitalRead(BUTTON_PIN) == HIGH) { // button being pressed

    servo.setAngle(0, 90); // move servo on channel 0 to 90 degrees

    for (int i = 0; i < NUM_LEDS; i++) {
        // makes a rainbow on the LED
        int hue = i * (65536 / NUM_LEDS);
        strip.setPixelColor(i, strip.ColorHSV(hue, 255, 255));
    }
    strip.show();

    tone(SPEAKER_PIN, 262, 500); // play 262Hz (middle C) for 500 milliseconds (doesn't sound like middle C lol)

  } else {
    // button not being pressed; reset everything
    servo.setAngle(0, 0);

    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(0, 0, 0));
    }
    strip.show();

    noTone(SPEAKER_PIN);
  }
}