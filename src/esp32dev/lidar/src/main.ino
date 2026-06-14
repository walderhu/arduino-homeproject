#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

#include "animate.h"

constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr int OLED_RESET = -1;

constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
constexpr uint8_t OLED_ADDR = 0x3C;

constexpr uint8_t SRC_WIDTH = 128;
constexpr uint8_t SRC_HEIGHT = 40;
constexpr uint8_t SRC_ROW_BYTES = (SRC_WIDTH + 7) / 8;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

const unsigned char* const BONGO_FRAMES[] = {
    _pawsonair,
    _leftpawontable,
    _pawsonair,
    _rightpawontable,
    _pawsonair,
    _leftpawontable,
    _rightpawontable,
    _pawsontable,
};

constexpr uint8_t FRAME_COUNT = sizeof(BONGO_FRAMES) / sizeof(BONGO_FRAMES[0]);

constexpr uint16_t FRAME_DELAYS_MS[] = {
    90, 65, 55, 65, 90, 65, 65, 110,
};

bool bitmapPixel(const unsigned char* bitmap, int16_t x, int16_t y) {
    const uint16_t byteIndex = static_cast<uint16_t>(y) * SRC_ROW_BYTES + (x / 8);
    const uint8_t bit = 7 - (x & 7);
    return pgm_read_byte(bitmap + byteIndex) & (1 << bit);
}

void drawFullScreenFrame(const unsigned char* frame) {
    display.clearDisplay();

    for (int16_t y = 0; y < OLED_HEIGHT; y++) {
        const int16_t srcY = (y * SRC_HEIGHT) / OLED_HEIGHT;
        for (int16_t x = 0; x < OLED_WIDTH; x++) {
            if (bitmapPixel(frame, x, srcY)) {
                display.drawPixel(x, y, SSD1306_WHITE);
            }
        }
    }

    display.display();
}

void setup() {
    Serial.begin(115200);
    delay(300);

    Wire.begin(I2C_SDA, I2C_SCL);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println(F("OLED not found at 0x3C. Try address 0x3D or check SDA/SCL."));
        while (true) {
            delay(1000);
        }
    }

    drawFullScreenFrame(_pawsonair);
}

void loop() {
    static uint8_t frameIndex = 0;

    drawFullScreenFrame(BONGO_FRAMES[frameIndex]);
    delay(FRAME_DELAYS_MS[frameIndex]);
    frameIndex = (frameIndex + 1) % FRAME_COUNT;
}
