#include <Arduino.h>

static constexpr uint8_t MOTOR_PWM_PIN = 13;
static constexpr uint8_t PWM_CHANNEL = 0;
static constexpr uint32_t PWM_FREQUENCY_HZ = 20000;
static constexpr uint8_t PWM_RESOLUTION_BITS = 10;
static constexpr uint16_t PWM_MAX_DUTY = (1U << PWM_RESOLUTION_BITS) - 1;

void setup() {
    Serial.begin(115200);

    ledcSetup(PWM_CHANNEL, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcAttachPin(MOTOR_PWM_PIN, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, PWM_MAX_DUTY);

    Serial.printf("Motor PWM: GPIO%u, %lu Hz, duty 100%% (%u/%u)\n",
                  MOTOR_PWM_PIN,
                  static_cast<unsigned long>(PWM_FREQUENCY_HZ),
                  PWM_MAX_DUTY,
                  PWM_MAX_DUTY);
}

void loop() {
    // Hardware PWM remains at maximum duty.
    delay(1000);
}

