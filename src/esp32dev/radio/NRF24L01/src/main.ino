#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <RF24.h>
#include <SPI.h>
#include <Wire.h>

constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint8_t NRF_CE_PIN = 4;
constexpr uint8_t NRF_CSN_PIN = 5;

LiquidCrystal_I2C lcd(0x27, 16, 2);
RF24 radio(NRF_CE_PIN, NRF_CSN_PIN);

void printLine(uint8_t row, const char *text) {
    lcd.setCursor(0, row);
    lcd.print("                ");
    lcd.setCursor(0, row);
    lcd.print(text);
}

uint16_t scanRadioNoise() {
    uint16_t hits = 0;

    for (uint8_t channel = 0; channel <= 125; channel++) {
        radio.setChannel(channel);
        radio.startListening();
        delayMicroseconds(180);

        if (radio.testRPD()) {
            hits++;
        }

        radio.stopListening();
    }

    return hits;
}

void setup() {
    Serial.begin(115200);

    Wire.begin(SDA_PIN, SCL_PIN);

    lcd.init();
    lcd.backlight();
    lcd.clear();

    printLine(0, "NRF24 tester");
    printLine(1, "Starting...");

    SPI.begin(18, 19, 23, NRF_CSN_PIN);

    if (!radio.begin()) {
        printLine(0, "NRF24 ERROR");
        printLine(1, "Check wiring");
        Serial.println("NRF24 not found. Check VCC/GND/CE/CSN/SCK/MOSI/MISO.");
        while (true) {
            delay(1000);
        }
    }

    radio.setAutoAck(false);
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_1MBPS);
    radio.disableCRC();
    radio.stopListening();

    printLine(0, "NRF24 OK");
    printLine(1, "Scan 2.4GHz");
    Serial.println("NRF24 OK. Scanning 2.4GHz activity...");
    delay(1000);
}

void loop() {
    uint16_t hits = scanRadioNoise();

    char line[17];
    snprintf(line, sizeof(line), "Hits:%3u / 126", hits);

    printLine(0, "2.4GHz activity");
    printLine(1, line);

    Serial.printf("2.4GHz activity hits: %u / 126\n", hits);
    delay(300);
}
