#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint8_t SIGNAL_PIN = 34;

constexpr uint8_t ADC_SAMPLES = 16;
constexpr uint32_t UPDATE_MS = 200;

// Коэффициент делителя напряжения: V_real = V_measured * VOLTAGE_COEFF
// 2.0 = потенциометр делит на 2 (4V на входе -> 2V на GPIO)
#define VOLTAGE_COEFF 2.0f

// 1.5V = 0%, 2.1V = 100%
constexpr float VOLTAGE_MIN = 1.5f;
constexpr float VOLTAGE_MAX = 2.1f;

LiquidCrystal_I2C lcd(0x27, 16, 2);

static void printLine(uint8_t row, const char *text) {
    lcd.setCursor(0, row);
    lcd.print("                ");
    lcd.setCursor(0, row);
    lcd.print(text);
}

static float readVoltage() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
        sum += analogReadMilliVolts(SIGNAL_PIN);
    }
    return (float)sum / ADC_SAMPLES / 1000.0f * VOLTAGE_COEFF;
}

static uint8_t voltageToPct(float v) {
    int pct = (int)((v - VOLTAGE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN) * 100.0f + 0.5f);
    if (pct > 100)
        pct = 100;
    if (pct < 0)
        pct = 0;
    return (uint8_t)pct;
}

void setup() {
    Serial.begin(115200);
    analogSetAttenuation(ADC_11db);

    Wire.begin(SDA_PIN, SCL_PIN);
    lcd.init();
    lcd.backlight();
    lcd.clear();

    printLine(0, "Voltage: ---");
    printLine(1, "Battery: ---");
}

void loop() {
    float v = readVoltage();
    uint8_t pct = voltageToPct(v);

    char row0[17], row1[17];
    snprintf(row0, sizeof(row0), "Voltage: %.2fV", v);
    snprintf(row1, sizeof(row1), "Battery: %3u%%", pct);

    printLine(0, row0);
    printLine(1, row1);

    Serial.printf("%.4f V  |  %u%%\n", v, pct);
    delay(UPDATE_MS);
}
