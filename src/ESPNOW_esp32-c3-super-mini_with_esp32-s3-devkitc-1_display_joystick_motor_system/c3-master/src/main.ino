#include <Arduino.h>

constexpr uint8_t MOTOR_PIN = 13;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("ESP32-S3 motor test started");

    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, LOW);
}

void loop() {
    digitalWrite(MOTOR_PIN, HIGH);
    Serial.println("Motor ON");
    delay(1000);

    digitalWrite(MOTOR_PIN, LOW);
    Serial.println("Motor OFF");
    delay(1000);
}
