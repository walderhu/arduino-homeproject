#include "config.h"

#include <stdio.h>

void printStateToSerial(const PocketState &state) {
    char num[8];

    auto printStick = [&](float value) {
        snprintf(num, sizeof(num), "%+.2f", value);
        Serial.print(num);
    };

    Serial.print("LJ(X:");
    printStick(state.ljX);
    Serial.print("|Y:");
    printStick(state.ljY);
    Serial.print(") RJ(X:");
    printStick(state.rjX);
    Serial.print("|Y:");
    printStick(state.rjY);
    Serial.print(") SA:");
    Serial.print(state.sa);
    Serial.print(" SB:");
    Serial.print(state.sb);
    Serial.print(" SC:");
    Serial.print(state.sc);
    Serial.print(" SD:");
    Serial.print(state.sd);
    Serial.print(" SE:");
    Serial.print(state.se);
    Serial.print(" S1:");
    Serial.print(state.s1, 2);
    Serial.print(" link:");
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

    elrsBegin();

    Serial.println();
    Serial.println("ELRS OLED monitor");
    Serial.println("OLED: SDA=21 SCL=22");
    Serial.print("CRSF RX: GPIO");
    Serial.print(CRSF_RX_PIN);
    Serial.print(" baud=");
    Serial.print(CRSF_BAUD);
    Serial.print(" inverted=");
    Serial.println(CRSF_UART_INVERTED ? "yes" : "no");
}

void loop() {
    elrsPoll();
    const PocketState &state = elrsGetState();

    static uint32_t lastDisplayMs = 0;
    static uint32_t lastSerialMs = 0;
    const uint32_t nowMs = millis();

    if (nowMs - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
        lastDisplayMs = nowMs;
        oledDraw(state);
    }

    if (nowMs - lastSerialMs >= SERIAL_PRINT_INTERVAL_MS) {
        lastSerialMs = nowMs;
        printStateToSerial(state);
    }
}
