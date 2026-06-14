#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <Wire.h>

#include "animate.h"
#include "lock.h"

constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr int OLED_RESET = -1;

constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
constexpr uint8_t OLED_ADDR = 0x3C;

constexpr uint8_t SRC_WIDTH = 128;
constexpr uint8_t SRC_HEIGHT = 40;
constexpr uint8_t SRC_ROW_BYTES = (SRC_WIDTH + 7) / 8;

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RETRY_DELAY_MS = 5000;

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

bool otaInProgress = false;
bool otaReady = false;
uint8_t frameIndex = 0;
uint32_t lastFrameMs = 0;

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

void printWifiStatus(wl_status_t status) {
    switch (status) {
        case WL_NO_SSID_AVAIL:
            Serial.println(F("reason: SSID not found (wrong name or 2.4 GHz only)"));
            break;
        case WL_CONNECT_FAILED:
            Serial.println(F("reason: wrong password or auth failed"));
            break;
        case WL_CONNECTION_LOST:
            Serial.println(F("reason: connection lost"));
            break;
        case WL_DISCONNECTED:
            Serial.println(F("reason: disconnected"));
            break;
        default:
            Serial.print(F("reason code: "));
            Serial.println(status);
            break;
    }
}

void printWifiDiagnostics() {
    Serial.print(F("Target SSID: "));
    Serial.println(ssid);
    Serial.print(F("ESP32 MAC: "));
    Serial.println(WiFi.macAddress());
    Serial.println(F("Scanning nearby networks..."));

    const int16_t networkCount = WiFi.scanNetworks();
    if (networkCount <= 0) {
        Serial.println(F("No networks found"));
        return;
    }

    bool ssidVisible = false;
    for (int16_t i = 0; i < networkCount; i++) {
        Serial.print(F("  "));
        Serial.print(WiFi.SSID(i));
        Serial.print(F(" ("));
        Serial.print(WiFi.RSSI(i));
        Serial.print(F(" dBm, "));
        Serial.print(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? F("open") : F("secured"));
        Serial.println(F(")"));

        if (WiFi.SSID(i) == ssid) {
            ssidVisible = true;
        }
    }

    if (!ssidVisible) {
        Serial.println(F("Target SSID is NOT visible to ESP32"));
    } else {
        Serial.println(F("Target SSID is visible to ESP32"));
    }
}

bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    Serial.println(F("Connecting to WiFi..."));
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(ssid, password);

    const uint32_t startedMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startedMs) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("WiFi connection failed"));
        printWifiStatus(WiFi.status());
        return false;
    }

    Serial.print(F("WiFi connected, IP: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("Router will show MAC: "));
    Serial.println(WiFi.macAddress());
    return true;
}

void setupOta() {
    if (otaReady) {
        return;
    }

    ArduinoOTA.setHostname(ota_hostname);
    ArduinoOTA.setPassword(ota_password);

    ArduinoOTA.onStart([]() {
        otaInProgress = true;
        Serial.println(F("OTA update started"));
    });

    ArduinoOTA.onEnd([]() {
        otaInProgress = false;
        Serial.println(F("OTA update finished"));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        otaInProgress = false;
        Serial.print(F("OTA error: "));
        Serial.println(error);
    });

    ArduinoOTA.begin();
    otaReady = true;
    Serial.print(F("OTA ready: "));
    Serial.print(ota_hostname);
    Serial.println(F(".local"));
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

    printWifiDiagnostics();

    if (connectWiFi()) {
        setupOta();
    } else {
        Serial.println(F("Continuing without OTA until WiFi is available"));
    }
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        static uint32_t lastRetryMs = 0;
        if (millis() - lastRetryMs >= WIFI_RETRY_DELAY_MS) {
            lastRetryMs = millis();
            if (connectWiFi()) {
                setupOta();
            }
        }
    } else if (otaReady) {
        ArduinoOTA.handle();
    }

    if (otaInProgress) {
        return;
    }

    const uint32_t now = millis();
    if (now - lastFrameMs >= FRAME_DELAYS_MS[frameIndex]) {
        drawFullScreenFrame(BONGO_FRAMES[frameIndex]);
        frameIndex = (frameIndex + 1) % FRAME_COUNT;
        lastFrameMs = now;
    }
}
