#include "config.h"
#include "servo.h"

namespace {

constexpr uint32_t SERVO_MAX_DUTY = (1u << SERVO_RESOLUTION_BITS) - 1u;
constexpr uint32_t SERVO_PERIOD_US = 1000000u / SERVO_FREQ_HZ;
float currentAngleDeg = 90.0f;

uint32_t pulseUsFromStick(float stick11) {
    const float stick = constrain(stick11, -1.0f, 1.0f);
    const float pulse = (SERVO_MIN_US + SERVO_MAX_US) * 0.5f +
                        stick * (SERVO_MAX_US - SERVO_MIN_US) * 0.5f;
    return static_cast<uint32_t>(pulse + 0.5f);
}

float angleDegFromStick(float stick11) {
    const float stick = constrain(stick11, -1.0f, 1.0f);
    return (stick + 1.0f) * 90.0f;
}

void writePulseUs(uint32_t pulseUs) {
    const uint32_t duty = static_cast<uint32_t>(
        (static_cast<uint64_t>(pulseUs) * SERVO_MAX_DUTY) / SERVO_PERIOD_US);
    ledcWrite(SERVO_CHANNEL, duty);
}

} // namespace

void servoBegin() {
    ledcSetup(SERVO_CHANNEL, SERVO_FREQ_HZ, SERVO_RESOLUTION_BITS);
    ledcAttachPin(SERVO_PIN, SERVO_CHANNEL);
    servoSetFromStick(0.0f);
}

void servoSetFromStick(float stick11) {
    currentAngleDeg = angleDegFromStick(stick11);
    writePulseUs(pulseUsFromStick(stick11));
}

float servoGetAngleDeg() { return currentAngleDeg; }
