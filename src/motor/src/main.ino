#include "config.h"
#include "pwm.h"
#include "servo.h"

#include <stdio.h>

static float powerFromRightStick(float rjY) {
    // Forward half only: 0 at center/back, 1 at full forward.
    return constrain(rjY, 0.0f, 1.0f);
}

static void printStateToSerial(const PocketState &state, float pwmDuty, float servoAngleDeg) {
    char num[8];

    auto printStick = [&](float value) {
        snprintf(num, sizeof(num), "%+.2f", value);
        Serial.print(num);
    };

    Serial.print("PWM:");
    Serial.print(static_cast<int>(pwmDuty * 100.0f + 0.5f));
    Serial.print("% RJ(Y:");
    printStick(state.rjY);
    Serial.print(") SRV:");
    Serial.print(static_cast<int>(servoAngleDeg + 0.5f));
    Serial.print("deg RJ(X:");
    printStick(state.rjX);
    Serial.print(") link:");
    Serial.println(state.linked ? "OK" : "NO");
}

void setup() {
    Serial.begin(SERIAL_MONITOR_BAUD);
    delay(300);

    if (!oledBegin()) {
        Serial.println("OLED not found at 0x3C. Try address 0x3D or check SDA/SCL.");
        while (true) {
            delay(1000);
        }
    }

    pwmBegin();
    servoBegin();
    elrsBegin();

    Serial.println();
    Serial.println("ELRS PWM controller");
    Serial.println("OLED: SDA=21 SCL=22");
    Serial.print("PWM: GPIO");
    Serial.print(PWM_PIN);
    Serial.print(" @");
    Serial.print(PWM_FREQ_HZ);
    Serial.println("Hz");
    Serial.print("CRSF RX: GPIO");
    Serial.print(CRSF_RX_PIN);
    Serial.print(" baud=");
    Serial.print(CRSF_BAUD);
    Serial.print(" inverted=");
    Serial.println(CRSF_UART_INVERTED ? "yes" : "no");
    Serial.println("Right stick Y forward = 0..100% PWM");
    Serial.print("Servo: GPIO");
    Serial.print(SERVO_PIN);
    Serial.println(" Right stick X = 0..180deg");
}

void loop() {
    elrsPoll();
    const PocketState &state = elrsGetState();

    float pwmDuty = 0.0f;
    float servoStick = 0.0f;
    if (state.linked) {
        pwmDuty = powerFromRightStick(state.rjY);
        servoStick = state.rjX;
    }
    pwmSetDuty(pwmDuty);
    servoSetFromStick(servoStick);

    static uint32_t lastDisplayMs = 0;
    static uint32_t lastSerialMs = 0;
    const uint32_t nowMs = millis();

    if (nowMs - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
        lastDisplayMs = nowMs;
        oledDraw(state, pwmDuty);
    }

    if (nowMs - lastSerialMs >= SERIAL_PRINT_INTERVAL_MS) {
        lastSerialMs = nowMs;
        printStateToSerial(state, pwmDuty, servoGetAngleDeg());
    }
}
