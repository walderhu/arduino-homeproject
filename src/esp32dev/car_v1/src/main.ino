#include <Arduino.h>

constexpr uint8_t MOTOR_PINS[] = {13, 12, 14, 27};
constexpr uint8_t MOTOR_CHANNELS[] = {0, 1, 2, 3};
constexpr uint8_t MOTOR_COUNT = sizeof(MOTOR_PINS) / sizeof(MOTOR_PINS[0]);

constexpr uint32_t PWM_FREQ_HZ = 1000;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
constexpr uint8_t FORWARD_SPEED = 180; // 0..255

void setupMotors() {
    for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
        ledcSetup(MOTOR_CHANNELS[i], PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
        ledcAttachPin(MOTOR_PINS[i], MOTOR_CHANNELS[i]);
        ledcWrite(MOTOR_CHANNELS[i], FORWARD_SPEED);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("=== Car forward PWM test START ===");

    setupMotors();
    Serial.println("Motors PWM enabled: forward");
}

void loop() {
    for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
        ledcWrite(MOTOR_CHANNELS[i], FORWARD_SPEED);
    }

    delay(1000);
}
