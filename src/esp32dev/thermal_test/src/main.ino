#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#include "ThermalSensor.h"

constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
constexpr uint8_t LCD_ADDR = 0x27;  // если пусто — попробуй 0x3F

constexpr uint8_t SENSOR_PIN = 4;
constexpr uint32_t READ_INTERVAL_MS = 1000;

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
ThermalSensor sensor(SENSOR_PIN);

uint32_t lastReadMs = 0;

void scanI2C() {
    Serial.println("I2C scan:");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X\n", addr);
            found++;
        }
    }
    if (found == 0) {
        Serial.println("  no devices");
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin(I2C_SDA, I2C_SCL);

    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.print("DS18B20 GPIO 4");
    delay(500);
    lcd.clear();

    scanI2C();
}

void loop() {
    const uint32_t now = millis();
    if (now - lastReadMs < READ_INTERVAL_MS) {
        delay(50);
        return;
    }
    lastReadMs = now;

    const float tempC = sensor.read();

    lcd.setCursor(0, 0);
    char line0[17];
    if (isnan(tempC)) {
        snprintf(line0, sizeof(line0), "GPIO%2u: no sensor", SENSOR_PIN);
    } else {
        snprintf(line0, sizeof(line0), "GPIO%2u: %5.1f C", SENSOR_PIN, tempC);
    }
    lcd.print(line0);

    lcd.setCursor(0, 1);
    lcd.print(isnan(tempC) ? "check wiring    " : "OK              ");

    if (isnan(tempC)) {
        Serial.printf("GPIO%u: no sensor\n", SENSOR_PIN);
    } else {
        Serial.printf("GPIO%u: %.1f C\n", SENSOR_PIN, tempC);
    }
}
