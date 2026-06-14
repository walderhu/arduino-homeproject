#include "config.h"
#include "pwm.h"

namespace {

constexpr uint32_t PWM_MAX_DUTY = (1u << PWM_RESOLUTION_BITS) - 1u;
float currentDuty = 0.0f;

} // namespace

void pwmBegin() {
    ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
    ledcAttachPin(PWM_PIN, PWM_CHANNEL);
    pwmSetDuty(0.0f);
}

void pwmSetDuty(float duty01) {
    currentDuty = constrain(duty01, 0.0f, 1.0f);
    const uint32_t rawDuty = static_cast<uint32_t>(currentDuty * static_cast<float>(PWM_MAX_DUTY) + 0.5f);
    ledcWrite(PWM_CHANNEL, rawDuty);
}

float pwmGetDuty() { return currentDuty; }
