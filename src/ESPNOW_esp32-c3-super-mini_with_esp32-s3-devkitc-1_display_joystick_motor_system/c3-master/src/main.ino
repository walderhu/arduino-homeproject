#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <esp_wifi.h>

constexpr uint8_t PIN_SW = 0;
constexpr uint8_t I2C_SDA = 8;
constexpr uint8_t I2C_SCL = 9;

constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint32_t PACKET_MAGIC = 0x4D4F5431; // "MOT1"
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t SEND_REPEAT_MS = 100;

const uint8_t S3_MAC[] = {0xD0, 0xCF, 0x13, 0x37, 0xC3, 0x90};

struct __attribute__((packed)) MotorPacket {
    uint32_t magic;
    uint8_t version;
    uint8_t motorOn;
    uint32_t sequence;
};

LiquidCrystal_I2C lcd(0x27, 16, 2);

bool motorOn = false;
bool rawButton = HIGH;
bool lastRawButton = HIGH;
bool buttonState = HIGH;
bool idleButton = HIGH;
uint32_t lastDebounceMs = 0;
uint32_t lastSendMs = 0;
uint32_t lastDebugMs = 0;
uint32_t sequence = 0;
esp_now_send_status_t lastSendStatus = ESP_NOW_SEND_FAIL;

bool isButtonPressed(bool level) { return level != idleButton; }

void drawStatus() {
    lcd.setCursor(0, 0);
    lcd.print("motor: ");
    lcd.print(motorOn ? "on " : "off");
    lcd.print("       ");

    lcd.setCursor(0, 1);
    lcd.print("raw:");
    lcd.print(rawButton ? "1 " : "0 ");
    lcd.print(isButtonPressed(buttonState) ? "dn " : "up ");
    lcd.print("tx:");
    lcd.print(lastSendStatus == ESP_NOW_SEND_SUCCESS ? "ok" : "--");
    lcd.print(" ");
}

void onDataSent(const uint8_t *, esp_now_send_status_t status) { lastSendStatus = status; }

void addS3Peer() {
    if (esp_now_is_peer_exist(S3_MAC)) {
        return;
    }

    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, S3_MAC, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    esp_err_t result = esp_now_add_peer(&peer);
    if (result != ESP_OK) {
        Serial.printf("ESP-NOW add S3 peer failed: %d\n", result);
    } else {
        Serial.println("ESP-NOW S3 peer added");
    }
}

void sendMotorState() {
    MotorPacket packet{
        .magic = PACKET_MAGIC,
        .version = 1,
        .motorOn = static_cast<uint8_t>(motorOn ? 1 : 0),
        .sequence = ++sequence,
    };

    esp_err_t result = esp_now_send(S3_MAC, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
    if (result != ESP_OK) {
        Serial.printf("ESP-NOW send failed: %d\n", result);
    }
}

void setupEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_send_cb(onDataSent);
    addS3Peer();
}

void setup() {
    Serial.begin(115200);
    delay(300);

    pinMode(PIN_SW, INPUT_PULLUP);
    rawButton = digitalRead(PIN_SW);
    lastRawButton = rawButton;
    buttonState = rawButton;
    idleButton = rawButton;

    Wire.begin(I2C_SDA, I2C_SCL);
    lcd.init();
    lcd.backlight();
    lcd.clear();

    setupEspNow();

    Serial.printf("C3 master channel=%u mac=%s target=D0:CF:13:37:C3:90 idle=%d\n", ESPNOW_CHANNEL,
                  WiFi.macAddress().c_str(), idleButton);
    drawStatus();
}

void loop() {
    rawButton = digitalRead(PIN_SW);

    if (rawButton != lastRawButton) {
        lastDebounceMs = millis();
        lastRawButton = rawButton;
    }

    if (millis() - lastDebounceMs > DEBOUNCE_MS && rawButton != buttonState) {
        buttonState = rawButton;

        if (isButtonPressed(buttonState)) {
            motorOn = !motorOn;
            Serial.printf("TOGGLE motor=%s raw=%d idle=%d\n", motorOn ? "on" : "off", rawButton,
                          idleButton);
        }

        drawStatus();
    }

    if (millis() - lastSendMs > SEND_REPEAT_MS) {
        lastSendMs = millis();
        sendMotorState();
        drawStatus();
    }

    if (millis() - lastDebugMs > 1000) {
        lastDebugMs = millis();
        Serial.printf("Debug raw=%d state=%d motor=%s tx=%d seq=%lu\n", rawButton, buttonState,
                      motorOn ? "on" : "off", lastSendStatus, static_cast<unsigned long>(sequence));
    }
}
