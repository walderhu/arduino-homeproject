#include "config.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <stdio.h>

namespace {

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

PocketState lastDrawn = {};
uint32_t lastDrawMs = 0;
uint32_t lastForceDrawMs = 0;
uint32_t oledDrawCount = 0;
uint32_t oledI2cFailCount = 0;
uint32_t oledRecoverCount = 0;

void formatSignedFloat2(char *buf, size_t len, float value) {
    snprintf(buf, len, "%+.2f", value);
}

int stickCenti(float value) { return static_cast<int>(value * 100.0f); }

bool pocketStateVisibleChange(const PocketState &current, const PocketState &previous) {
    if (current.linked != previous.linked) {
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

void drawStickWidget(int16_t x, int16_t y, int16_t w, int16_t h, float stickX, float stickY) {
    display.drawRect(x, y, w, h, SSD1306_WHITE);
    display.drawLine(x + w / 2, y, x + w / 2, y + h - 1, SSD1306_WHITE);
    display.drawLine(x, y + h / 2, x + w - 1, y + h / 2, SSD1306_WHITE);

    const int16_t dotX = x + (w - 1) / 2 + static_cast<int16_t>(stickX * ((w - 5) / 2));
    const int16_t dotY = y + (h - 1) / 2 - static_cast<int16_t>(stickY * ((h - 5) / 2));
    display.fillCircle(dotX, dotY, 2, SSD1306_WHITE);
}

void drawSwitchAt(int16_t x, int16_t y, const char *label, uint8_t value, bool threeState) {
    display.setCursor(x, y);
    display.print(label);
    display.print(':');
    if (threeState) {
        display.print(value);
    } else {
        display.print(value ? "1" : "0");
    }
}

void renderScreen(const PocketState &state) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

    display.setCursor(0, 0);
    display.print("ELRS Pocket");
    display.setCursor(88, 0);
    display.print(state.linked ? "LINK" : "----");

    if (!state.linked) {
        display.setCursor(0, 14);
        display.println("No CRSF signal");
        display.println("ELRS TX -> GPIO16");
        display.print("inv=");
        display.println(CRSF_UART_INVERTED ? "yes" : "no");
        display.display();
        return;
    }

    char num[8];

    display.setCursor(0, 10);
    display.print("LJ X:");
    formatSignedFloat2(num, sizeof(num), state.ljX);
    display.print(num);
    display.print(" Y:");
    formatSignedFloat2(num, sizeof(num), state.ljY);
    display.print(num);

    display.setCursor(0, 19);
    display.print("RJ X:");
    formatSignedFloat2(num, sizeof(num), state.rjX);
    display.print(num);
    display.print(" Y:");
    formatSignedFloat2(num, sizeof(num), state.rjY);
    display.print(num);

    drawSwitchAt(0, 30, "SA", state.sa, false);
    drawSwitchAt(30, 30, "SB", state.sb, true);
    drawSwitchAt(60, 30, "SC", state.sc, true);

    drawSwitchAt(0, 39, "SD", state.sd, false);
    drawSwitchAt(30, 39, "SE", state.se, false);
    display.setCursor(60, 39);
    display.print("S1:");
    display.print(static_cast<int>(state.s1 * 100.0f));
    display.print('%');

    drawStickWidget(0, 50, 28, 13, state.ljX, state.ljY);
    drawStickWidget(36, 50, 28, 13, state.rjX, state.rjY);

    display.display();
}

} // namespace

bool oledBegin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_CLOCK_HZ);
    return display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
}

void oledDraw(const PocketState &state) {
    const uint32_t nowMs = millis();
    const bool changed = pocketStateVisibleChange(state, lastDrawn);
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

    renderScreen(state);
    oledDrawCount++;
    lastDrawn = state;
    lastDrawMs = nowMs;
    if (forceRefresh) {
        lastForceDrawMs = nowMs;
    }
}
