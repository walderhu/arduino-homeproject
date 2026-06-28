#include <Arduino.h>

static constexpr uint8_t SERVO_PIN = 13;
static constexpr uint8_t PWM_CHANNEL = 0;
static constexpr uint8_t PWM_RESOLUTION_BITS = 16;
static constexpr uint16_t PWM_FREQUENCY_HZ = 50;
static constexpr uint16_t SERVO_MIN_PULSE_US = 500;
static constexpr uint16_t SERVO_MAX_PULSE_US = 2500;
static constexpr uint32_t PWM_PERIOD_US = 1000000UL / PWM_FREQUENCY_HZ;
static constexpr uint32_t PWM_MAX_DUTY = (1UL << PWM_RESOLUTION_BITS) - 1;

uint32_t pulseUsToDuty(uint16_t pulseUs) {
    return static_cast<uint32_t>(pulseUs) * PWM_MAX_DUTY / PWM_PERIOD_US;
}

uint16_t angleToPulseUs(uint8_t angle) {
    return SERVO_MIN_PULSE_US +
           static_cast<uint32_t>(angle) *
               (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) / 180;
}

void setServoAngle(uint8_t angle) {
    const uint16_t pulseUs = angleToPulseUs(angle);
    ledcWrite(PWM_CHANNEL, pulseUsToDuty(pulseUs));
    Serial.printf("Servo: %u degrees (%u us)\n", angle, pulseUs);
}

void setup() {
    Serial.begin(115200);

    ledcSetup(PWM_CHANNEL, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcAttachPin(SERVO_PIN, PWM_CHANNEL);
    Serial.println("MG996R sequence: 85 -> 90 -> 95 degrees");
}

void loop() {
    setServoAngle(85);
    delay(1000);

    setServoAngle(90);
    delay(1000);

    setServoAngle(95);
    delay(1000);
}
