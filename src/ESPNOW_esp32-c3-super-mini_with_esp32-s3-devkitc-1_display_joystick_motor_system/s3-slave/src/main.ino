#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

constexpr uint8_t MOTOR_PIN = 13;
constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint32_t PACKET_MAGIC = 0x4D4F5431; // "MOT1"
constexpr uint32_t COMMAND_TIMEOUT_MS = 1000;

struct __attribute__((packed)) MotorPacket {
    uint32_t magic;
    uint8_t version;
    uint8_t motorOn;
    uint32_t sequence;
};

bool motorOn = false;
uint32_t lastPacketMs = 0;
uint32_t lastSequence = 0;

void applyMotorState(bool enabled) {
    motorOn = enabled;
    digitalWrite(MOTOR_PIN, motorOn ? HIGH : LOW);
}

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    if (len != sizeof(MotorPacket)) {
        Serial.printf("RX ignored len=%d\n", len);
        return;
    }

    MotorPacket packet{};
    memcpy(&packet, incomingData, sizeof(packet));

    if (packet.magic != PACKET_MAGIC || packet.version != 1) {
        Serial.printf("RX ignored magic=0x%08lX version=%u\n",
                      static_cast<unsigned long>(packet.magic), packet.version);
        return;
    }

    applyMotorState(packet.motorOn != 0);
    lastPacketMs = millis();
    lastSequence = packet.sequence;

    Serial.printf("RX %02X:%02X:%02X:%02X:%02X:%02X motor=%s seq=%lu\n", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5], motorOn ? "on" : "off",
                  static_cast<unsigned long>(lastSequence));
}

void setupEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        Serial0.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);
}

void setup() {
    Serial.begin(115200);
    Serial0.begin(115200);
    Serial.setDebugOutput(true);
    delay(300);

    pinMode(MOTOR_PIN, OUTPUT);
    applyMotorState(false);

    setupEspNow();

    Serial.printf("S3 slave channel=%u mac=%s\n", ESPNOW_CHANNEL, WiFi.macAddress().c_str());
    Serial0.printf("S3 slave channel=%u mac=%s\n", ESPNOW_CHANNEL, WiFi.macAddress().c_str());
}

void loop() {
    if (motorOn && millis() - lastPacketMs > COMMAND_TIMEOUT_MS) {
        applyMotorState(false);
        Serial.println("Motor OFF: command timeout");
        Serial0.println("Motor OFF: command timeout");
    }

    static uint32_t lastLogMs = 0;
    if (millis() - lastLogMs > 1000) {
        lastLogMs = millis();
        Serial.printf("Debug motor=%s lastSeq=%lu age=%lu\n", motorOn ? "on" : "off",
                      static_cast<unsigned long>(lastSequence),
                      static_cast<unsigned long>(millis() - lastPacketMs));
    }
}
