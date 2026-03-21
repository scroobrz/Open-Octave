#include "PCA9685.h"
#include <Wire.h>

ServoDriver servo;


void setup() {
    // join I2C bus (I2Cdev library doesn't do this automatically)
    Wire.begin();
    Serial.begin(9600);
    servo.init(0x7f);
    // uncomment this line if you need to use a special servo
    // servo.setServoPulseRange(600,2400,180);
}

void loop() {
    for (int i = 1; i < 17; i++) {
        servo.setAngle(i, 0);
    }
}