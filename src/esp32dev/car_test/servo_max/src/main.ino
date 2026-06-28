#include <Arduino.h>

static constexpr uint8_t SERVO_PIN = 13;
static constexpr uint8_t PWM_CHANNEL = 0;
static constexpr uint8_t PWM_RESOLUTION_BITS = 16;
static constexpr uint16_t PWM_FREQUENCY_HZ = 50;
static constexpr uint16_t MAX_SERVO_PULSE_US = 2500;
static constexpr uint32_t PWM_PERIOD_US = 1000000UL / PWM_FREQUENCY_HZ;
static constexpr uint32_t PWM_MAX_DUTY = (1UL << PWM_RESOLUTION_BITS) - 1;

uint32_t pulseUsToDuty(uint16_t pulseUs) {
    return static_cast<uint32_t>(pulseUs) * PWM_MAX_DUTY / PWM_PERIOD_US;
}

void setup() {
    Serial.begin(115200);

    ledcSetup(PWM_CHANNEL, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcAttachPin(SERVO_PIN, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, pulseUsToDuty(MAX_SERVO_PULSE_US));

    Serial.println("GPIO13: maximum servo command, 2500 us at 50 Hz");
}

void loop() {
    // LEDC hardware continuously holds the maximum servo command.
    delay(1000);
}

