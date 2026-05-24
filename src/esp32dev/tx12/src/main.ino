#include <Arduino.h>

/*
  RadioMaster TX12 external module bay reader for ESP32.

  Wiring:
    Bay pin 1 PPM / RC signal  -> ESP32 GPIO34 through a 3.3V level shifter/divider
    Bay pin 5 signal/data      -> ESP32 GPIO16 through a 3.3V level shifter/divider
    Bay pin 4 GND              -> ESP32 GND

  Do not connect bay +6V or VBAT directly to any ESP32 GPIO.
  Power the ESP32 from USB or a proper regulator.

  In EdgeTX/OpenTX:
    - Set External RF to PPM to test the PPM decoder.
    - Set External RF to CRSF to test the UART decoder.
    - Use serial monitor commands: crsf, crsfinv, sbus, ibus, raw420, raw100, raw115.
*/

// ================= USER SETTINGS =================
static constexpr uint8_t PPM_PIN = 34;       // input-only GPIO, good for PPM
static constexpr uint8_t MODULE_RX_PIN = 16; // ESP32 RX from module bay data line
static constexpr int8_t MODULE_TX_PIN = -1;  // unused; this sketch only listens

static constexpr uint32_t SERIAL_MONITOR_BAUD = 115200;
static constexpr uint32_t MODULE_UART_BAUD = 420000; // CRSF default
static constexpr bool MODULE_UART_INVERTED = true;   // TX12 bay CRSF is inverted here

static constexpr uint8_t MAX_CHANNELS = 16;
static constexpr uint16_t CRSF_RAW_MIN = 173;
static constexpr uint16_t CRSF_RAW_MAX = 1811;
static constexpr uint16_t PPM_MIN_US = 800;
static constexpr uint16_t PPM_MAX_US = 2400;
static constexpr uint16_t PPM_FRAME_GAP_US = 3500;
static constexpr uint32_t PRINT_INTERVAL_MS = 200;
static constexpr uint32_t SIGNAL_TIMEOUT_MS = 1000;
static constexpr bool PRINT_PPM_STATUS = false;
static constexpr bool PRINT_RAW_CHANNELS = false;

// ================= PPM DECODER =================
struct PpmEdgeDecoder {
    uint32_t lastEdgeUs = 0;
    uint16_t frame[MAX_CHANNELS] = {};
    uint8_t channelIndex = 0;
};

struct PpmSnapshot {
    uint16_t channels[MAX_CHANNELS] = {};
    uint8_t count = 0;
    uint32_t frameCounter = 0;
    uint32_t lastFrameMs = 0;
};

PpmEdgeDecoder ppmRising;
PpmEdgeDecoder ppmFalling;
PpmSnapshot ppmData;

portMUX_TYPE ppmMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR processPpmEdge(PpmEdgeDecoder &decoder, uint32_t nowUs) {
    const uint32_t width = nowUs - decoder.lastEdgeUs;
    decoder.lastEdgeUs = nowUs;

    if (width > PPM_FRAME_GAP_US) {
        if (decoder.channelIndex >= 4) {
            portENTER_CRITICAL_ISR(&ppmMux);
            ppmData.count = min<uint8_t>(decoder.channelIndex, MAX_CHANNELS);
            for (uint8_t i = 0; i < ppmData.count; ++i) {
                ppmData.channels[i] = decoder.frame[i];
            }
            ppmData.frameCounter++;
            ppmData.lastFrameMs = millis();
            portEXIT_CRITICAL_ISR(&ppmMux);
        }
        decoder.channelIndex = 0;
        return;
    }

    if (width >= PPM_MIN_US && width <= PPM_MAX_US && decoder.channelIndex < MAX_CHANNELS) {
        decoder.frame[decoder.channelIndex++] = static_cast<uint16_t>(width);
    }
}

void IRAM_ATTR ppmInterrupt() {
    const uint32_t nowUs = micros();
    if (digitalRead(PPM_PIN) == HIGH) {
        processPpmEdge(ppmRising, nowUs);
    } else {
        processPpmEdge(ppmFalling, nowUs);
    }
}

// ================= CRSF DECODER =================
HardwareSerial ModuleSerial(2);

enum class SerialProfile : uint8_t {
    Crsf,
    CrsfInverted,
    Sbus,
    Ibus,
    Raw420,
    Raw100,
    Raw115,
};

struct SerialProfileConfig {
    SerialProfile profile;
    const char *name;
    uint32_t baud;
    uint32_t config;
    bool inverted;
};

static constexpr SerialProfileConfig SERIAL_PROFILES[] = {
    {SerialProfile::Crsf, "crsf", 420000, SERIAL_8N1, false},
    {SerialProfile::CrsfInverted, "crsfinv", 420000, SERIAL_8N1, true},
    {SerialProfile::Sbus, "sbus", 100000, SERIAL_8E2, true},
    {SerialProfile::Ibus, "ibus", 115200, SERIAL_8N1, false},
    {SerialProfile::Raw420, "raw420", 420000, SERIAL_8N1, false},
    {SerialProfile::Raw100, "raw100", 100000, SERIAL_8N1, false},
    {SerialProfile::Raw115, "raw115", 115200, SERIAL_8N1, false},
};

const SerialProfileConfig *activeSerialProfile = &SERIAL_PROFILES[1];
String commandLine;

void printRawSample();

bool isCrsfProfile() {
    return activeSerialProfile->profile == SerialProfile::Crsf ||
           activeSerialProfile->profile == SerialProfile::CrsfInverted;
}

static constexpr uint8_t CRSF_FRAMETYPE_RC_CHANNELS_PACKED = 0x16;
static constexpr uint8_t CRSF_MAX_PACKET_SIZE = 64;
static constexpr uint8_t SBUS_FRAME_SIZE = 25;
static constexpr uint8_t IBUS_FRAME_SIZE = 32;

uint8_t crsfBuf[CRSF_MAX_PACKET_SIZE] = {};
uint8_t crsfPos = 0;
uint8_t crsfExpected = 0;

uint16_t crsfChannels[MAX_CHANNELS] = {};
uint32_t crsfFrameCounter = 0;
uint32_t crsfLastFrameMs = 0;
uint32_t uartByteCounter = 0;
uint32_t crsfPacketCounter = 0;
uint32_t crsfCrcFailCounter = 0;
uint32_t crsfTypeCounter[256] = {};
uint8_t rawSample[32] = {};
uint8_t rawSampleCount = 0;

uint8_t sbusBuf[SBUS_FRAME_SIZE] = {};
uint8_t sbusPos = 0;
uint16_t sbusChannels[MAX_CHANNELS] = {};
uint32_t sbusFrameCounter = 0;
uint32_t sbusLastFrameMs = 0;
uint32_t sbusBadFrameCounter = 0;

uint8_t ibusBuf[IBUS_FRAME_SIZE] = {};
uint8_t ibusPos = 0;
uint16_t ibusChannels[14] = {};
uint32_t ibusFrameCounter = 0;
uint32_t ibusLastFrameMs = 0;
uint32_t ibusBadFrameCounter = 0;

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

void decodeCrsfPacket(const uint8_t *packet, uint8_t packetSize) {
    if (packetSize < 4) {
        return;
    }

    const uint8_t length = packet[1];
    const uint8_t type = packet[2];
    const uint8_t crc = packet[1 + length];

    if (crc8D5(&packet[2], length - 1) != crc) {
        crsfCrcFailCounter++;
        return;
    }

    crsfPacketCounter++;
    crsfTypeCounter[type]++;

    if (type != CRSF_FRAMETYPE_RC_CHANNELS_PACKED || length != 24) {
        return;
    }

    const uint8_t *payload = &packet[3];
    for (uint8_t ch = 0; ch < MAX_CHANNELS; ++ch) {
        crsfChannels[ch] = read11Bits(payload, ch);
    }
    crsfFrameCounter++;
    crsfLastFrameMs = millis();
}

void resetSerialDecoders() {
    crsfPos = 0;
    crsfExpected = 0;
    sbusPos = 0;
    ibusPos = 0;
    rawSampleCount = 0;
}

void startModuleSerial(uint8_t profileIndex) {
    const SerialProfileConfig &profile = SERIAL_PROFILES[profileIndex];
    activeSerialProfile = &profile;
    resetSerialDecoders();
    ModuleSerial.end();
    delay(20);
    ModuleSerial.begin(profile.baud, profile.config, MODULE_RX_PIN, MODULE_TX_PIN,
                       profile.inverted);

    Serial.print("Serial profile: ");
    Serial.print(profile.name);
    Serial.print(" baud=");
    Serial.print(profile.baud);
    Serial.print(" inverted=");
    Serial.println(profile.inverted ? "yes" : "no");
}

void decodeSbusFrame(const uint8_t *frame) {
    if (frame[0] != 0x0F || (frame[24] != 0x00 && frame[24] != 0x04)) {
        sbusBadFrameCounter++;
        return;
    }

    sbusChannels[0] = ((frame[1] | frame[2] << 8) & 0x07FF);
    sbusChannels[1] = ((frame[2] >> 3 | frame[3] << 5) & 0x07FF);
    sbusChannels[2] = ((frame[3] >> 6 | frame[4] << 2 | frame[5] << 10) & 0x07FF);
    sbusChannels[3] = ((frame[5] >> 1 | frame[6] << 7) & 0x07FF);
    sbusChannels[4] = ((frame[6] >> 4 | frame[7] << 4) & 0x07FF);
    sbusChannels[5] = ((frame[7] >> 7 | frame[8] << 1 | frame[9] << 9) & 0x07FF);
    sbusChannels[6] = ((frame[9] >> 2 | frame[10] << 6) & 0x07FF);
    sbusChannels[7] = ((frame[10] >> 5 | frame[11] << 3) & 0x07FF);
    sbusChannels[8] = ((frame[12] | frame[13] << 8) & 0x07FF);
    sbusChannels[9] = ((frame[13] >> 3 | frame[14] << 5) & 0x07FF);
    sbusChannels[10] = ((frame[14] >> 6 | frame[15] << 2 | frame[16] << 10) & 0x07FF);
    sbusChannels[11] = ((frame[16] >> 1 | frame[17] << 7) & 0x07FF);
    sbusChannels[12] = ((frame[17] >> 4 | frame[18] << 4) & 0x07FF);
    sbusChannels[13] = ((frame[18] >> 7 | frame[19] << 1 | frame[20] << 9) & 0x07FF);
    sbusChannels[14] = ((frame[20] >> 2 | frame[21] << 6) & 0x07FF);
    sbusChannels[15] = ((frame[21] >> 5 | frame[22] << 3) & 0x07FF);
    sbusFrameCounter++;
    sbusLastFrameMs = millis();
}

void pollSbusByte(uint8_t b) {
    if (sbusPos == 0 && b != 0x0F) {
        return;
    }

    sbusBuf[sbusPos++] = b;
    if (sbusPos >= SBUS_FRAME_SIZE) {
        decodeSbusFrame(sbusBuf);
        sbusPos = 0;
    }
}

void decodeIbusFrame(const uint8_t *frame) {
    if (frame[0] != 0x20 || frame[1] != 0x40) {
        ibusBadFrameCounter++;
        return;
    }

    uint16_t checksum = 0xFFFF;
    for (uint8_t i = 0; i < IBUS_FRAME_SIZE - 2; ++i) {
        checksum -= frame[i];
    }
    const uint16_t received = frame[30] | (frame[31] << 8);
    if (checksum != received) {
        ibusBadFrameCounter++;
        return;
    }

    for (uint8_t ch = 0; ch < 14; ++ch) {
        ibusChannels[ch] = frame[2 + ch * 2] | (frame[3 + ch * 2] << 8);
    }
    ibusFrameCounter++;
    ibusLastFrameMs = millis();
}

void pollIbusByte(uint8_t b) {
    if (ibusPos == 0 && b != 0x20) {
        return;
    }
    if (ibusPos == 1 && b != 0x40) {
        ibusPos = 0;
        return;
    }

    ibusBuf[ibusPos++] = b;
    if (ibusPos >= IBUS_FRAME_SIZE) {
        decodeIbusFrame(ibusBuf);
        ibusPos = 0;
    }
}

void pollSerialProtocol() {
    while (ModuleSerial.available()) {
        const uint8_t b = static_cast<uint8_t>(ModuleSerial.read());
        uartByteCounter++;
        if (rawSampleCount < sizeof(rawSample)) {
            rawSample[rawSampleCount++] = b;
        }

        if (activeSerialProfile->profile == SerialProfile::Sbus) {
            pollSbusByte(b);
            continue;
        }

        if (activeSerialProfile->profile == SerialProfile::Ibus) {
            pollIbusByte(b);
            continue;
        }

        if (!isCrsfProfile()) {
            continue;
        }

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
            crsfExpected = b + 2; // address + length + body(length bytes)
            continue;
        }

        crsfBuf[crsfPos++] = b;
        if (crsfExpected > 0 && crsfPos >= crsfExpected) {
            decodeCrsfPacket(crsfBuf, crsfExpected);
            crsfPos = 0;
            crsfExpected = 0;
        }
    }
}

// ================= OUTPUT =================
void printChannels(const char *label, const uint16_t *channels, uint8_t count) {
    Serial.print(label);
    Serial.print(": ");
    for (uint8_t i = 0; i < count; ++i) {
        Serial.print("ch");
        Serial.print(i + 1);
        Serial.print('=');
        Serial.print(channels[i]);
        if (i + 1 < count) {
            Serial.print(' ');
        }
    }
    Serial.println();
}

float normalizeCrsf01(uint16_t raw) {
    const float value = (static_cast<float>(raw) - CRSF_RAW_MIN) /
                        (CRSF_RAW_MAX - CRSF_RAW_MIN);
    return constrain(value, 0.0f, 1.0f);
}

uint8_t normalizeCrsfButton(uint16_t raw) {
    return normalizeCrsf01(raw) >= 0.5f ? 1 : 0;
}

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

void printFloat2(float value) {
    Serial.print(value, 2);
}

void printTx12StateFromCrsf() {
    const float rjX = normalizeCrsf01(crsfChannels[0]); // CH1: roll/aileron
    const float rjY = normalizeCrsf01(crsfChannels[1]); // CH2: pitch/elevator
    const float ljY = normalizeCrsf01(crsfChannels[2]); // CH3: throttle
    const float ljX = normalizeCrsf01(crsfChannels[3]); // CH4: yaw/rudder

    const uint8_t a = normalizeCrsfButton(crsfChannels[4]);
    const uint8_t b = normalizeCrsfThreeState(crsfChannels[5]);
    const uint8_t c = normalizeCrsfThreeState(crsfChannels[6]);
    const uint8_t d = normalizeCrsfButton(crsfChannels[7]);
    const uint8_t e = normalizeCrsfThreeState(crsfChannels[8]);
    const uint8_t f = normalizeCrsfThreeState(crsfChannels[9]);
    const float s1 = normalizeCrsf01(crsfChannels[10]);
    const float s2 = normalizeCrsf01(crsfChannels[11]);

    Serial.print("LJ(X:");
    printFloat2(ljX);
    Serial.print("|Y:");
    printFloat2(ljY);
    Serial.print(") RJ(X:");
    printFloat2(rjX);
    Serial.print("|Y:");
    printFloat2(rjY);
    Serial.print(") A:");
    Serial.print(a);
    Serial.print(" B:");
    Serial.print(b);
    Serial.print(" C:");
    Serial.print(c);
    Serial.print(" D:");
    Serial.print(d);
    Serial.print(" E:");
    Serial.print(e);
    Serial.print(" F:");
    Serial.print(f);
    Serial.print(" S1:");
    printFloat2(s1);
    Serial.print(" S2:");
    printFloat2(s2);
    Serial.println();
}

void printStatus() {
    static uint32_t lastPrintMs = 0;
    static uint32_t lastPpmCounter = 0;
    static uint32_t lastCrsfCounter = 0;
    static uint32_t lastUartByteCounter = 0;
    static uint32_t lastCrsfPacketCounter = 0;
    static uint32_t lastCrsfCrcFailCounter = 0;
    static uint32_t lastSbusCounter = 0;
    static uint32_t lastIbusCounter = 0;

    const uint32_t nowMs = millis();
    if (nowMs - lastPrintMs < PRINT_INTERVAL_MS) {
        return;
    }
    lastPrintMs = nowMs;

    uint16_t ppmChannels[MAX_CHANNELS] = {};
    uint8_t ppmCount = 0;
    uint32_t ppmCounter = 0;
    uint32_t ppmLastMs = 0;

    portENTER_CRITICAL(&ppmMux);
    ppmCount = ppmData.count;
    ppmCounter = ppmData.frameCounter;
    ppmLastMs = ppmData.lastFrameMs;
    for (uint8_t i = 0; i < ppmCount; ++i) {
        ppmChannels[i] = ppmData.channels[i];
    }
    portEXIT_CRITICAL(&ppmMux);

    if (PRINT_PPM_STATUS && ppmCounter != lastPpmCounter) {
        printChannels("PPM", ppmChannels, ppmCount);
        lastPpmCounter = ppmCounter;
    } else if (PRINT_PPM_STATUS && (ppmLastMs == 0 || nowMs - ppmLastMs > SIGNAL_TIMEOUT_MS)) {
        Serial.println("PPM: no valid frame");
    }

    if (activeSerialProfile->profile == SerialProfile::Sbus) {
        if (sbusFrameCounter != lastSbusCounter) {
            printChannels("SBUS raw", sbusChannels, MAX_CHANNELS);
            lastSbusCounter = sbusFrameCounter;
        } else if (sbusLastFrameMs == 0 || nowMs - sbusLastFrameMs > SIGNAL_TIMEOUT_MS) {
            Serial.print("SBUS: no frame bad=");
            Serial.print(sbusBadFrameCounter);
            printRawSample();
        }
        return;
    }

    if (activeSerialProfile->profile == SerialProfile::Ibus) {
        if (ibusFrameCounter != lastIbusCounter) {
            printChannels("IBUS", ibusChannels, 14);
            lastIbusCounter = ibusFrameCounter;
        } else if (ibusLastFrameMs == 0 || nowMs - ibusLastFrameMs > SIGNAL_TIMEOUT_MS) {
            Serial.print("IBUS: no frame bad=");
            Serial.print(ibusBadFrameCounter);
            printRawSample();
        }
        return;
    }

    if (!isCrsfProfile()) {
        Serial.print("RAW ");
        Serial.print(activeSerialProfile->name);
        Serial.print(" bytes=");
        Serial.print(uartByteCounter - lastUartByteCounter);
        printRawSample();
        lastUartByteCounter = uartByteCounter;
        return;
    }

    if (crsfFrameCounter != lastCrsfCounter) {
        if (PRINT_RAW_CHANNELS) {
            printChannels("CRSF raw", crsfChannels, MAX_CHANNELS);
        } else {
            printTx12StateFromCrsf();
        }
        lastCrsfCounter = crsfFrameCounter;
    } else if (crsfLastFrameMs == 0 || nowMs - crsfLastFrameMs > SIGNAL_TIMEOUT_MS) {
        Serial.print("CRSF: no RC frame");
        if (uartByteCounter != lastUartByteCounter) {
            Serial.print(" (UART bytes seen: ");
            Serial.print(uartByteCounter - lastUartByteCounter);
            Serial.print(')');
        }
        if (crsfPacketCounter != lastCrsfPacketCounter || crsfCrcFailCounter != lastCrsfCrcFailCounter) {
            Serial.print(" packets=");
            Serial.print(crsfPacketCounter - lastCrsfPacketCounter);
            Serial.print(" crcFail=");
            Serial.print(crsfCrcFailCounter - lastCrsfCrcFailCounter);
            Serial.print(" types:");
            uint8_t printed = 0;
            for (uint16_t type = 0; type < 256 && printed < 6; ++type) {
                if (crsfTypeCounter[type] > 0) {
                    Serial.print(" 0x");
                    if (type < 0x10) {
                        Serial.print('0');
                    }
                    Serial.print(type, HEX);
                    Serial.print('=');
                    Serial.print(crsfTypeCounter[type]);
                    printed++;
                }
            }
        }
        printRawSample();
        Serial.println();
    }
    lastUartByteCounter = uartByteCounter;
    lastCrsfPacketCounter = crsfPacketCounter;
    lastCrsfCrcFailCounter = crsfCrcFailCounter;
}

void printRawSample() {
    if (rawSampleCount == 0) {
        Serial.println();
        return;
    }

    Serial.print(" raw:");
    for (uint8_t i = 0; i < rawSampleCount; ++i) {
        Serial.print(' ');
        if (rawSample[i] < 0x10) {
            Serial.print('0');
        }
        Serial.print(rawSample[i], HEX);
    }
    rawSampleCount = 0;
    Serial.println();
}

void handleCommand(const String &command) {
    for (uint8_t i = 0; i < sizeof(SERIAL_PROFILES) / sizeof(SERIAL_PROFILES[0]); ++i) {
        if (command == SERIAL_PROFILES[i].name) {
            startModuleSerial(i);
            return;
        }
    }

    Serial.println("Commands: crsf, crsfinv, sbus, ibus, raw420, raw100, raw115");
}

void pollCommands() {
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            commandLine.trim();
            commandLine.toLowerCase();
            if (commandLine.length() > 0) {
                handleCommand(commandLine);
            }
            commandLine = "";
            continue;
        }
        if (commandLine.length() < 32) {
            commandLine += c;
        }
    }
}

void setup() {
    Serial.begin(SERIAL_MONITOR_BAUD);
    delay(300);

    Serial.println();
    Serial.println("TX12 external module bay reader");
    Serial.println("PPM input: GPIO34, UART RX: GPIO16, GND common, GPIO max 3.3V");
    Serial.println("Commands: crsf, crsfinv, sbus, ibus, raw420, raw100, raw115");

    pinMode(PPM_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(PPM_PIN), ppmInterrupt, CHANGE);

    ModuleSerial.begin(MODULE_UART_BAUD, SERIAL_8N1, MODULE_RX_PIN, MODULE_TX_PIN,
                       MODULE_UART_INVERTED);
    Serial.println("Default serial profile: crsfinv");
}

void loop() {
    pollCommands();
    pollSerialProtocol();
    printStatus();
}
