#include "config.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <stdio.h>

namespace {

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

PocketState lastDrawn = {};
float lastDrawnDuty = -1.0f;
uint32_t lastDrawMs = 0;
uint32_t lastForceDrawMs = 0;
uint32_t oledDrawCount = 0;
uint32_t oledI2cFailCount = 0;
uint32_t oledRecoverCount = 0;

void formatSignedFloat2(char *buf, size_t len, float value) {
    snprintf(buf, len, "%+.2f", value);
}

int stickCenti(float value) { return static_cast<int>(value * 100.0f); }

bool pocketStateVisibleChange(const PocketState &current, const PocketState &previous, float pwmDuty,
                              float previousDuty) {
    if (current.linked != previous.linked) {
        return true;
    }

    if (stickCenti(pwmDuty) != stickCenti(previousDuty)) {
        return true;
    }

    if (!current.linked) {
        return false;
    }

    if (current.sa != previous.sa || current.sb != previous.sb || current.sc != previous.sc ||
        current.sd != previous.sd || current.se != previous.se) {
        return true;
    }

    if (stickCenti(current.s1) != stickCenti(previous.s1)) {
        return true;
    }

    if (stickCenti(current.ljX) != stickCenti(previous.ljX) ||
        stickCenti(current.ljY) != stickCenti(previous.ljY) ||
        stickCenti(current.rjX) != stickCenti(previous.rjX) ||
        stickCenti(current.rjY) != stickCenti(previous.rjY)) {
        return true;
    }

    return false;
}

bool oledI2cReady() {
    Wire.beginTransmission(OLED_ADDR);
    return Wire.endTransmission() == 0;
}

bool oledRecoverIfNeeded() {
    if (oledI2cReady()) {
        return true;
    }

    oledI2cFailCount++;
    oledRecoverCount++;
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_CLOCK_HZ);
    return display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
}

void renderScreen(const PocketState &state, float pwmDuty) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

    display.setCursor(0, 0);
    display.print("PWM Motor");
    display.setCursor(88, 0);
    display.print(state.linked ? "LINK" : "----");

    if (!state.linked) {
        display.setCursor(0, 14);
        display.println("No CRSF signal");
        display.println("PWM: 0%");
        display.println("ELRS TX -> GPIO16");
        display.display();
        return;
    }

    const int pwmPercent = static_cast<int>(pwmDuty * 100.0f + 0.5f);

    display.setTextSize(2);
    display.setCursor(0, 12);
    display.print(pwmPercent);
    display.print('%');

    display.setTextSize(1);
    display.setCursor(0, 34);
    display.print("RJ Y:");
    char num[8];
    formatSignedFloat2(num, sizeof(num), state.rjY);
    display.print(num);

    constexpr int16_t barX = 0;
    constexpr int16_t barY = 44;
    constexpr int16_t barW = 120;
    constexpr int16_t barH = 8;
    display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
    const int16_t fillW = static_cast<int16_t>((barW - 2) * pwmDuty);
    if (fillW > 0) {
        display.fillRect(barX + 1, barY + 1, fillW, barH - 2, SSD1306_WHITE);
    }

    display.setCursor(0, 55);
    display.print("GPIO");
    display.print(PWM_PIN);
    display.print(" @");
    display.print(PWM_FREQ_HZ);
    display.print("Hz");

    display.display();
}

} // namespace

bool oledBegin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_CLOCK_HZ);
    return display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
}

void oledDraw(const PocketState &state, float pwmDuty) {
    const uint32_t nowMs = millis();
    const bool changed = pocketStateVisibleChange(state, lastDrawn, pwmDuty, lastDrawnDuty);
    const bool forceRefresh = (nowMs - lastForceDrawMs) >= DISPLAY_FORCE_REFRESH_MS;
    const bool intervalElapsed = (nowMs - lastDrawMs) >= DISPLAY_INTERVAL_MS;

    if (!changed && !forceRefresh) {
        return;
    }
    if (!intervalElapsed && !forceRefresh) {
        return;
    }

    // #region agent log
    const bool i2cOkBefore = oledI2cReady();
    if (!i2cOkBefore) {
        Serial.print("DBG hyp=A i2c=NACK beforeDraw draws=");
        Serial.print(oledDrawCount);
        Serial.print(" recoveries=");
        Serial.println(oledRecoverCount);
    }
    // #endregion

    if (!oledRecoverIfNeeded()) {
        // #region agent log
        Serial.println("DBG hyp=B oled recover FAILED");
        // #endregion
        return;
    }

    renderScreen(state, pwmDuty);
    oledDrawCount++;
    lastDrawn = state;
    lastDrawnDuty = pwmDuty;
    lastDrawMs = nowMs;
    if (forceRefresh) {
        lastForceDrawMs = nowMs;
    }
}
