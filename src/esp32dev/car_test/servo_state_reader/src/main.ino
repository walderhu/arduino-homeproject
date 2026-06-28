#include <Arduino.h>

// Connect only the servo's analog feedback signal here.
// Kept strictly as an input: this firmware never generates servo PWM.
static constexpr uint8_t SERVO_FEEDBACK_PIN = 13;
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint32_t PRINT_INTERVAL_MS = 100;
static constexpr uint8_t SAMPLE_COUNT = 16;
static constexpr uint16_t ADC_MAX = 4095;
static constexpr float ADC_REFERENCE_V = 3.3f;

uint16_t readAveragedFeedback() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < SAMPLE_COUNT; ++i) {
        sum += analogRead(SERVO_FEEDBACK_PIN);
    }
    return static_cast<uint16_t>(sum / SAMPLE_COUNT);
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    pinMode(SERVO_FEEDBACK_PIN, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(SERVO_FEEDBACK_PIN, ADC_11db);

    Serial.println();
    Serial.println("Passive servo feedback reader");
    Serial.println("No servo control signal is generated.");
    Serial.println("time_ms,raw,voltage_v,position_percent");
}

void loop() {
    static uint32_t nextPrintMs = 0;
    const uint32_t now = millis();

    if (static_cast<int32_t>(now - nextPrintMs) < 0) {
        return;
    }
    nextPrintMs = now + PRINT_INTERVAL_MS;

    const uint16_t raw = readAveragedFeedback();
    const float voltage = raw * ADC_REFERENCE_V / ADC_MAX;
    const float positionPercent = raw * 100.0f / ADC_MAX;

    Serial.printf("%lu,%u,%.3f,%.1f\n",
                  static_cast<unsigned long>(now), raw, voltage, positionPercent);
}
