// node-b: ESP32 DEV + NRF24L01 + LED + кнопка (без экрана)
// Зеркальная логика node-a: оба узла мастер и слейв.
#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>

#include "../../common/protocol.h"

constexpr uint8_t NRF_CE   = 4;
constexpr uint8_t NRF_CSN  = 5;
constexpr uint8_t NRF_SCK  = 18;
constexpr uint8_t NRF_MISO = 19;
constexpr uint8_t NRF_MOSI = 23;

constexpr uint8_t LED_PIN    = 2;
constexpr uint8_t BUTTON_PIN = 15;

constexpr uint8_t SELF_ID = 2;
constexpr uint8_t PEER_ID = 1;

RF24 radio(NRF_CE, NRF_CSN);

uint32_t txSeq = 0;
uint32_t lastAutoSend = 0;
uint8_t  lastButton = HIGH;

uint8_t  blinkLeft = 0;
uint16_t blinkLen  = 200;
uint32_t blinkPhaseEnd = 0;
bool     ledState = false;

void startBlink(uint8_t count, uint16_t lenMs) {
    blinkLeft = count * 2;
    blinkLen  = lenMs;
    blinkPhaseEnd = millis();
}

void tickBlink() {
    if (blinkLeft == 0) {
        if (ledState) { ledState = false; digitalWrite(LED_PIN, LOW); }
        return;
    }
    if ((int32_t)(millis() - blinkPhaseEnd) >= 0) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        blinkPhaseEnd = millis() + blinkLen;
        blinkLeft--;
    }
}

void sendBlink(uint8_t count = 3, uint16_t lenMs = 150) {
    Packet p{
        .cmd = CMD_BLINK,
        .fromId = SELF_ID,
        .blinkMs = lenMs,
        .blinkCount = count,
        .seq = ++txSeq,
        .uptimeMs = millis(),
    };
    radio.stopListening();
    bool ok = radio.write(&p, sizeof(p));
    radio.startListening();
    Serial.printf("B TX BLINK seq=%lu -> %s\n", p.seq, ok ? "OK" : "FAIL");
}

void handlePacket(const Packet& p) {
    Serial.printf("B RX cmd=0x%02X from=%u seq=%lu\n", p.cmd, p.fromId, p.seq);
    if (p.cmd == CMD_BLINK) {
        startBlink(p.blinkCount ? p.blinkCount : 3,
                   p.blinkMs ? p.blinkMs : 150);
        Packet ack{
            .cmd = CMD_ACK,
            .fromId = SELF_ID,
            .blinkMs = p.blinkMs,
            .blinkCount = p.blinkCount,
            .seq = p.seq,
            .uptimeMs = millis(),
        };
        radio.stopListening();
        radio.write(&ack, sizeof(ack));
        radio.startListening();
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    SPI.begin(NRF_SCK, NRF_MISO, NRF_MOSI, NRF_CSN);
    if (!radio.begin()) {
        Serial.println("NRF24 not found");
        while (true) delay(1000);
    }
    radio.setChannel(RF_CHANNEL);
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_1MBPS);
    radio.setRetries(5, 15);
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.openWritingPipe(PIPE_A);     // пишем в node-a
    radio.openReadingPipe(1, PIPE_B);  // читаем свой
    radio.startListening();

    Serial.println("NODE-B ready");
}

void loop() {
    while (radio.available()) {
        Packet p{};
        radio.read(&p, sizeof(p));
        handlePacket(p);
    }

    uint8_t btn = digitalRead(BUTTON_PIN);
    if (lastButton == HIGH && btn == LOW) {
        sendBlink(5, 120);
    }
    lastButton = btn;

    if (millis() - lastAutoSend >= SEND_PERIOD) {
        lastAutoSend = millis();
        sendBlink(2, 200);
    }

    tickBlink();
    delay(20);
}
