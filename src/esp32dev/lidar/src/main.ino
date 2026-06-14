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

constexpr uint8_t TRIG_PIN = 17;
constexpr uint8_t ECHO_PIN = 16;

constexpr uint16_t DIST_MIN_CM = 2;
constexpr uint16_t DIST_MAX_CM = 400;
constexpr uint32_t DATA_STALE_MS = 900;
constexpr uint32_t MEASURE_INTERVAL_MS = 80;
constexpr uint32_t ECHO_TIMEOUT_US = 25000;

struct SonarReading {
    uint16_t distanceCm = 0;
    uint32_t echoMicros = 0;
    bool valid = false;
};

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

SonarReading latest{};
uint32_t lastReadingMs = 0;
uint32_t lastMeasureMs = 0;
uint32_t lastFrameMs = 0;

bool hasFreshReading() {
    return latest.valid && (millis() - lastReadingMs) <= DATA_STALE_MS;
}

bool measureOnce(uint16_t& distanceCm, uint32_t& echoMicros) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    echoMicros = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
    if (echoMicros == 0) {
        return false;
    }

    distanceCm = echoMicros / 58;
    return distanceCm >= DIST_MIN_CM && distanceCm <= DIST_MAX_CM;
}

bool pollSonar() {
    const uint32_t now = millis();
    if (now - lastMeasureMs < MEASURE_INTERVAL_MS) {
        return false;
    }
    lastMeasureMs = now;

    uint16_t samples[3];
    uint32_t echoes[3];
    uint8_t validCount = 0;

    for (uint8_t i = 0; i < 3; i++) {
        uint16_t distanceCm = 0;
        uint32_t echoMicros = 0;
        if (measureOnce(distanceCm, echoMicros)) {
            samples[validCount] = distanceCm;
            echoes[validCount] = echoMicros;
            validCount++;
        }
        delay(10);
    }

    if (validCount == 0) {
        latest.valid = false;
        return true;
    }

    for (uint8_t i = 0; i + 1 < validCount; i++) {
        for (uint8_t j = i + 1; j < validCount; j++) {
            if (samples[j] < samples[i]) {
                const uint16_t swapD = samples[i];
                samples[i] = samples[j];
                samples[j] = swapD;

                const uint32_t swapE = echoes[i];
                echoes[i] = echoes[j];
                echoes[j] = swapE;
            }
        }
    }

    latest.distanceCm = samples[validCount / 2];
    latest.echoMicros = echoes[validCount / 2];
    latest.valid = true;
    lastReadingMs = now;
    return true;
}

void drawCenteredText(const char* text, int16_t y, uint8_t textSize) {
    display.setTextSize(textSize);
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t textWidth = 0;
    uint16_t textHeight = 0;
    display.getTextBounds(text, 0, y, &x1, &y1, &textWidth, &textHeight);

    const int16_t x = (OLED_WIDTH - static_cast<int16_t>(textWidth)) / 2;
    display.setCursor(x, y);
    display.print(text);
}

void drawScreen() {
    char distanceLine[12];

    if (hasFreshReading()) {
        snprintf(distanceLine, sizeof(distanceLine), "%ucm", latest.distanceCm);
    } else {
        snprintf(distanceLine, sizeof(distanceLine), "--cm");
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    drawCenteredText(distanceLine, 20, 3);
    display.display();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);

    Wire.begin(I2C_SDA, I2C_SCL);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println(F("OLED init failed"));
        while (true) {
            delay(1000);
        }
    }

    drawScreen();

    Serial.println(F("HC-SR04 ultrasonic sensor ready"));
    Serial.print(F("TRIG GPIO "));
    Serial.println(TRIG_PIN);
    Serial.print(F("ECHO GPIO "));
    Serial.println(ECHO_PIN);
}

void loop() {
    if (pollSonar()) {
        if (latest.valid) {
            Serial.print(F("distance="));
            Serial.print(latest.distanceCm);
            Serial.print(F(" cm, echo="));
            Serial.print(latest.echoMicros);
            Serial.println(F(" us"));
        } else {
            Serial.println(F("no echo"));
        }
    }

    const uint32_t now = millis();
    if (now - lastFrameMs >= 100) {
        drawScreen();
        lastFrameMs = now;
    }
}
