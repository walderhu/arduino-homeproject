#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr int OLED_RESET = -1;

constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
constexpr uint8_t OLED_ADDR = 0x3C;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

void showTestScreen() {
    display.clearDisplay();

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ESP32 OLED test");
    display.println("128x64 I2C");
    display.println();
    display.print("SDA: GPIO ");
    display.println(I2C_SDA);
    display.print("SCL: GPIO ");
    display.println(I2C_SCL);
    display.print("Addr: 0x");
    display.println(OLED_ADDR, HEX);

    display.drawRect(0, 54, 128, 10, SSD1306_WHITE);
    display.fillRect(2, 56, 124, 6, SSD1306_WHITE);
    display.display();
}

void setup() {
    Serial.begin(115200);
    delay(300);

    Wire.begin(I2C_SDA, I2C_SCL);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("OLED not found at 0x3C. Try address 0x3D or check SDA/SCL.");
        while (true) {
            delay(1000);
        }
    }

    showTestScreen();
}

void loop() {
    static uint32_t counter = 0;

    display.fillRect(0, 40, 128, 12, SSD1306_BLACK);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 42);
    display.print("Seconds: ");
    display.print(counter++);
    display.display();

    delay(1000);
}
