#include <Arduino.h>

constexpr uint8_t MOTOR_PIN = 13;

void setup() {
    Serial.begin(115200);
    Serial0.begin(115200);
    Serial.setDebugOutput(true);
    delay(500);

    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, LOW);

    Serial.println("S3 motor test started");
    Serial0.println("S3 motor test started");
}

void loop() {
    digitalWrite(MOTOR_PIN, HIGH);
    Serial.println("Motor ON");
    Serial0.println("Motor ON");
    delay(1000);

    digitalWrite(MOTOR_PIN, LOW);
    Serial.println("Motor OFF");
    Serial0.println("Motor OFF");
    delay(1000);
}
