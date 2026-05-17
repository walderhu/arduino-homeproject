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

// Таблица: напряжение (mV) -> процент заряда, шаг 5%
// 4.4V=100% ... 2.4V=0%, 21 точка
struct BatPoint {
    uint16_t mv;
    uint8_t pct;
};
static const BatPoint BAT_TABLE[] = {
    {4400, 100}, {4300, 95}, {4200, 90}, {4100, 85}, {4000, 80}, {3900, 75}, {3800, 70},
    {3700, 65},  {3600, 60}, {3500, 55}, {3400, 50}, {3300, 45}, {3200, 40}, {3100, 35},
    {3000, 30},  {2900, 25}, {2800, 20}, {2700, 15}, {2600, 10}, {2500, 5},  {2400, 0}};
constexpr uint8_t BAT_TABLE_LEN = sizeof(BAT_TABLE) / sizeof(BAT_TABLE[0]);

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

// Линейная интерполяция между точками таблицы -> 1% точность
static uint8_t voltageToPct(float v) {
    uint16_t mv = (uint16_t)(v * 1000.0f + 0.5f);

    if (mv >= BAT_TABLE[0].mv)
        return 100;
    if (mv <= BAT_TABLE[BAT_TABLE_LEN - 1].mv)
        return 0;

    for (uint8_t i = 0; i < BAT_TABLE_LEN - 1; i++) {
        const BatPoint &hi = BAT_TABLE[i];
        const BatPoint &lo = BAT_TABLE[i + 1];
        if (mv <= hi.mv && mv >= lo.mv) {
            // интерполяция
            uint16_t span_mv = hi.mv - lo.mv;   // 100 mV
            uint8_t span_pct = hi.pct - lo.pct; // 5
            uint16_t delta_mv = mv - lo.mv;
            return lo.pct + (uint8_t)((uint32_t)delta_mv * span_pct / span_mv);
        }
    }
    return 0;
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
