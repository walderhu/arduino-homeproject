// node-a: ESP32 DEV + NRF24L01 + OLED SSD1306 + LED + кнопка
// Двунаправленная связь с node-b. На OLED — статистика обмена и состояния.
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include <Wire.h>

#include "../../common/protocol.h"

// --- Пины радио (SPI) ---
constexpr uint8_t NRF_CE   = 4;
constexpr uint8_t NRF_CSN  = 5;
constexpr uint8_t NRF_SCK  = 18;
constexpr uint8_t NRF_MISO = 19;
constexpr uint8_t NRF_MOSI = 23;

// --- OLED I2C ---
constexpr uint8_t OLED_SDA = 21;
constexpr uint8_t OLED_SCL = 22;
constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint8_t OLED_W = 128;
constexpr uint8_t OLED_H = 64;

// --- LED + кнопка ---
constexpr uint8_t LED_PIN    = 2;   // встроенный синий на большинстве ESP32 DEV
constexpr uint8_t BUTTON_PIN = 15;  // кнопка на GND, INPUT_PULLUP

constexpr uint8_t SELF_ID = 1;
constexpr uint8_t PEER_ID = 2;

Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);
RF24 radio(NRF_CE, NRF_CSN);

uint32_t txSeq = 0;
uint32_t rxCount = 0;
uint32_t blinkRequests = 0;     // сколько раз попросили моргнуть
uint32_t blinkExecuted = 0;     // сколько раз сами моргнули по чужой команде
uint32_t lastAutoSend = 0;
uint32_t lastRxMs = 0;
uint8_t  lastPeerCmd = 0;
uint8_t  lastButton  = HIGH;

// Неблокирующее мигание
uint8_t  blinkLeft = 0;
uint16_t blinkLen  = 200;
uint32_t blinkPhaseEnd = 0;
bool     ledState = false;

void startBlink(uint8_t count, uint16_t lenMs) {
    blinkLeft = count * 2;            // включить+выключить за каждое мигание
    blinkLen  = lenMs;
    blinkPhaseEnd = millis();         // переключим на следующем тике
    blinkExecuted++;
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
    blinkRequests++;
    Serial.printf("A TX BLINK seq=%lu -> %s\n", p.seq, ok ? "OK" : "FAIL");
}

void handlePacket(const Packet& p) {
    rxCount++;
    lastRxMs = millis();
    lastPeerCmd = p.cmd;
    Serial.printf("A RX cmd=0x%02X from=%u seq=%lu\n", p.cmd, p.fromId, p.seq);
    if (p.cmd == CMD_BLINK) {
        startBlink(p.blinkCount ? p.blinkCount : 3,
                   p.blinkMs ? p.blinkMs : 150);
        // ACK обратно
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

void drawDisplay() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("NODE-A  NRF24 link");
    display.drawFastHLine(0, 9, OLED_W, SSD1306_WHITE);

    display.setCursor(0, 12);
    display.printf("TX req: %lu\n", blinkRequests);
    display.printf("RX pkt: %lu\n", rxCount);
    display.printf("Blinks: %lu\n", blinkExecuted);
    display.printf("PeerCmd: 0x%02X\n", lastPeerCmd);

    uint32_t age = lastRxMs ? (millis() - lastRxMs) / 1000 : 0;
    display.printf("LastRx: %lus\n", age);
    display.printf("LED: %s", ledState ? "ON" : "off");
    display.display();
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("OLED not found");
        while (true) delay(1000);
    }

    SPI.begin(NRF_SCK, NRF_MISO, NRF_MOSI, NRF_CSN);
    if (!radio.begin()) {
        Serial.println("NRF24 not found");
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("NRF24 FAIL");
        display.display();
        while (true) delay(1000);
    }
    radio.setChannel(RF_CHANNEL);
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_1MBPS);
    radio.setRetries(5, 15);
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.openWritingPipe(PIPE_B);     // пишем в node-b
    radio.openReadingPipe(1, PIPE_A);  // читаем свой
    radio.startListening();

    Serial.println("NODE-A ready");
}

void loop() {
    // приём
    while (radio.available()) {
        Packet p{};
        radio.read(&p, sizeof(p));
        handlePacket(p);
    }

    // кнопка — отправить «мигни 5 раз»
    uint8_t btn = digitalRead(BUTTON_PIN);
    if (lastButton == HIGH && btn == LOW) {
        sendBlink(5, 120);
    }
    lastButton = btn;

    // автопериодическая команда «мигни»
    if (millis() - lastAutoSend >= SEND_PERIOD) {
        lastAutoSend = millis();
        sendBlink(2, 200);
    }

    tickBlink();
    drawDisplay();
    delay(20);
}
