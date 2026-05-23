#include <Arduino.h>

// pwm 9
// pin1 10
// pin2 11

int in1Pin = 10;    // Define L293D channel 1 pin
int in2Pin = 11;    // Define L293D channel 2 pin
int enable1Pin = 9; // Define L293D enable 1 pin
int channel = 0;
boolean rotationDir; // Define a variable to save the motor's rotation direction
int rotationSpeed;   // Define a variable to save the motor rotation speed

void driveMotor(boolean dir, int spd) {
    if (dir) { // Control motor rotation direction
        digitalWrite(in1Pin, HIGH);
        digitalWrite(in2Pin, LOW);
    } else {
        digitalWrite(in1Pin, LOW);
        digitalWrite(in2Pin, HIGH);
    }
    ledcWrite(channel, spd); // Control motor rotation speed
}

void setup() {
    // Initialize the pin into an output mode:
    pinMode(in1Pin, OUTPUT);
    pinMode(in2Pin, OUTPUT);
    pinMode(enable1Pin, OUTPUT);
    ledcSetup(channel, 1000, 11); // Set PWM to 11 bits, range is 0-2047
    ledcAttachPin(enable1Pin, channel);
}

void loop() {
    int potenVal = analogRead(16); // Convert the voltage of rotary potentiometer into digital
    rotationSpeed = potenVal - 2048;
    if (potenVal > 2048)
        rotationDir = true;
    else
        rotationDir = false;

    // Calculate the motor speed
    rotationSpeed = abs(potenVal - 2048);
    // Control the steering and speed of the motor
    Serial.println(rotationSpeed);
    delay(100);
    driveMotor(rotationDir, constrain(rotationSpeed, 0, 2048));
}
