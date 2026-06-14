#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>

// ================= OLED =================
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr int OLED_RESET = -1;

constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
constexpr uint8_t OLED_ADDR = 0x3C;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ================= ELRS / CRSF =================
// ELRS nano receiver TX -> ESP32 RX (3.3V only, common GND)
constexpr uint8_t CRSF_RX_PIN = 16;
constexpr int8_t CRSF_TX_PIN = -1;
constexpr uint32_t CRSF_BAUD = 420000;
constexpr bool CRSF_UART_INVERTED = false; // true = crsfinv, like Pocket module bay

constexpr uint8_t MAX_CHANNELS = 16;
constexpr uint16_t CRSF_RAW_MIN = 173;
constexpr uint16_t CRSF_RAW_MAX = 1811;
constexpr uint32_t LINK_TIMEOUT_MS = 1000;
constexpr uint32_t DISPLAY_INTERVAL_MS = 50;

HardwareSerial CrsfSerial(2);

static constexpr uint8_t CRSF_FRAMETYPE_RC_CHANNELS_PACKED = 0x16;
static constexpr uint8_t CRSF_MAX_PACKET_SIZE = 64;

uint8_t crsfBuf[CRSF_MAX_PACKET_SIZE] = {};
uint8_t crsfPos = 0;
uint8_t crsfExpected = 0;

uint16_t crsfChannels[MAX_CHANNELS] = {};
uint32_t crsfLastFrameMs = 0;

struct PocketState {
    float ljX = 0.0f;
    float ljY = 0.0f;
    float rjX = 0.0f;
    float rjY = 0.0f;
    uint8_t sa = 0;
    uint8_t sb = 0;
    uint8_t sc = 0;
    uint8_t sd = 0;
    uint8_t se = 0;
    float s1 = 0.0f;
    bool linked = false;
};

PocketState pocketState;

bool isCrsfAddress(uint8_t value) {
    switch (value) {
    case 0xC8: // flight controller
    case 0xEA: // radio transmitter
    case 0xEC: // receiver
    case 0xEE: // CRSF transmitter/module
    case 0x00: // broadcast
        return true;
    default:
        return false;
    }
}

uint8_t crc8D5(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; ++i) {
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0xD5)
                               : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

uint16_t read11Bits(const uint8_t *payload, uint8_t channel) {
    const uint16_t bitOffset = channel * 11;
    const uint8_t byteOffset = bitOffset / 8;
    const uint8_t bitShift = bitOffset % 8;
    const uint32_t value = static_cast<uint32_t>(payload[byteOffset]) |
                           (static_cast<uint32_t>(payload[byteOffset + 1]) << 8) |
                           (static_cast<uint32_t>(payload[byteOffset + 2]) << 16);
    return static_cast<uint16_t>((value >> bitShift) & 0x07FF);
}

float normalizeCrsf01(uint16_t raw) {
    const float value = (static_cast<float>(raw) - CRSF_RAW_MIN) / (CRSF_RAW_MAX - CRSF_RAW_MIN);
    return constrain(value, 0.0f, 1.0f);
}

float normalizeCrsfStick(uint16_t raw) { return normalizeCrsf01(raw) * 2.0f - 1.0f; }

uint8_t normalizeCrsfButton(uint16_t raw) { return normalizeCrsf01(raw) >= 0.5f ? 1 : 0; }

uint8_t normalizeCrsfThreeState(uint16_t raw) {
    const float value = normalizeCrsf01(raw);
    if (value < 0.33f) {
        return 0;
    }
    if (value < 0.66f) {
        return 1;
    }
    return 2;
}

void updatePocketStateFromCrsf() {
    pocketState.rjX = normalizeCrsfStick(crsfChannels[0]);     // CH1
    pocketState.rjY = normalizeCrsfStick(crsfChannels[1]);     // CH2
    pocketState.ljY = normalizeCrsfStick(crsfChannels[2]);     // CH3 throttle
    pocketState.ljX = normalizeCrsfStick(crsfChannels[3]);     // CH4
    pocketState.sa = normalizeCrsfButton(crsfChannels[4]);     // CH5
    pocketState.sb = normalizeCrsfThreeState(crsfChannels[5]); // CH6
    pocketState.sc = normalizeCrsfThreeState(crsfChannels[6]); // CH7
    pocketState.sd = normalizeCrsfButton(crsfChannels[7]);     // CH8
    pocketState.se = normalizeCrsfButton(crsfChannels[8]);     // CH9
    pocketState.s1 = normalizeCrsf01(crsfChannels[9]);         // CH10
    pocketState.linked = true;
}

void decodeCrsfPacket(const uint8_t *packet, uint8_t packetSize) {
    if (packetSize < 4) {
        return;
    }

    const uint8_t length = packet[1];
    const uint8_t type = packet[2];
    const uint8_t crc = packet[1 + length];

    if (crc8D5(&packet[2], length - 1) != crc) {
        return;
    }

    if (type != CRSF_FRAMETYPE_RC_CHANNELS_PACKED || length != 24) {
        return;
    }

    const uint8_t *payload = &packet[3];
    for (uint8_t ch = 0; ch < MAX_CHANNELS; ++ch) {
        crsfChannels[ch] = read11Bits(payload, ch);
    }
    crsfLastFrameMs = millis();
    updatePocketStateFromCrsf();
}

void pollCrsf() {
    while (CrsfSerial.available()) {
        const uint8_t b = static_cast<uint8_t>(CrsfSerial.read());

        if (crsfPos == 0) {
            if (!isCrsfAddress(b)) {
                continue;
            }
            crsfBuf[crsfPos++] = b;
            continue;
        }

        if (crsfPos == 1) {
            if (b < 2 || b > CRSF_MAX_PACKET_SIZE - 2) {
                crsfPos = 0;
                crsfExpected = 0;
                continue;
            }
            crsfBuf[crsfPos++] = b;
            crsfExpected = b + 2;
            continue;
        }

        crsfBuf[crsfPos++] = b;
        if (crsfExpected > 0 && crsfPos >= crsfExpected) {
            decodeCrsfPacket(crsfBuf, crsfExpected);
            crsfPos = 0;
            crsfExpected = 0;
        }
    }

    if (crsfLastFrameMs == 0 || millis() - crsfLastFrameMs > LINK_TIMEOUT_MS) {
        pocketState.linked = false;
    }
}

void formatSignedFloat2(char *buf, size_t len, float value) {
    snprintf(buf, len, "%+.2f", value);
}

void printPocketStateToSerial() {
    char num[8];

    Serial.print("LJ(X:");
    formatSignedFloat2(num, sizeof(num), pocketState.ljX);
    Serial.print(num);
    Serial.print("|Y:");
    formatSignedFloat2(num, sizeof(num), pocketState.ljY);
    Serial.print(num);
    Serial.print(") RJ(X:");
    formatSignedFloat2(num, sizeof(num), pocketState.rjX);
    Serial.print(num);
    Serial.print("|Y:");
    formatSignedFloat2(num, sizeof(num), pocketState.rjY);
    Serial.print(num);
    Serial.print(") SA:");
    Serial.print(pocketState.sa);
    Serial.print(" SB:");
    Serial.print(pocketState.sb);
    Serial.print(" SC:");
    Serial.print(pocketState.sc);
    Serial.print(" SD:");
    Serial.print(pocketState.sd);
    Serial.print(" SE:");
    Serial.print(pocketState.se);
    Serial.print(" S1:");
    Serial.print(pocketState.s1, 2);
    Serial.print(" link:");
    Serial.println(pocketState.linked ? "OK" : "NO");
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

void drawRcScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.print("ELRS Pocket");
    display.setCursor(88, 0);
    display.print(pocketState.linked ? "LINK" : "----");

    if (!pocketState.linked) {
        display.setCursor(0, 14);
        display.println("No CRSF signal");
        display.println("RX GPIO16 @420k");
        display.print("inv=");
        display.println(CRSF_UART_INVERTED ? "yes" : "no");
        display.display();
        return;
    }

    char num[8];

    display.setCursor(0, 10);
    display.print("LJ X:");
    formatSignedFloat2(num, sizeof(num), pocketState.ljX);
    display.print(num);
    display.print(" Y:");
    formatSignedFloat2(num, sizeof(num), pocketState.ljY);
    display.print(num);

    display.setCursor(0, 19);
    display.print("RJ X:");
    formatSignedFloat2(num, sizeof(num), pocketState.rjX);
    display.print(num);
    display.print(" Y:");
    formatSignedFloat2(num, sizeof(num), pocketState.rjY);
    display.print(num);

    drawSwitchAt(0, 30, "SA", pocketState.sa, false);
    drawSwitchAt(30, 30, "SB", pocketState.sb, true);
    drawSwitchAt(60, 30, "SC", pocketState.sc, true);

    drawSwitchAt(0, 39, "SD", pocketState.sd, false);
    drawSwitchAt(30, 39, "SE", pocketState.se, false);
    display.setCursor(60, 39);
    display.print("S1:");
    display.print(static_cast<int>(pocketState.s1 * 100.0f));
    display.print('%');

    drawStickWidget(0, 50, 28, 13, pocketState.ljX, pocketState.ljY);
    drawStickWidget(36, 50, 28, 13, pocketState.rjX, pocketState.rjY);

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

    CrsfSerial.begin(CRSF_BAUD, SERIAL_8N1, CRSF_RX_PIN, CRSF_TX_PIN, CRSF_UART_INVERTED);

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
    pollCrsf();

    static uint32_t lastDisplayMs = 0;
    static uint32_t lastSerialMs = 0;
    const uint32_t nowMs = millis();

    if (nowMs - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
        lastDisplayMs = nowMs;
        drawRcScreen();
    }

    if (nowMs - lastSerialMs >= 200) {
        lastSerialMs = nowMs;
        printPocketStateToSerial();
    }
}
