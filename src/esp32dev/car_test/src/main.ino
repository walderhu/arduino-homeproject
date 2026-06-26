#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

/*
  RadioMaster Pocket external module bay reader for ESP32.

  Wiring:
    OLED SSD1306 I2C 128x64       -> SDA GPIO21, SCL GPIO22, VCC 3V3, GND
    Pocket nano bay PPM / RC signal  -> ESP32 GPIO34 through a 3.3V level shifter/divider
    Pocket nano bay CRSF/data signal -> ESP32 GPIO16 through a 3.3V level shifter/divider
    Pocket nano bay GND              -> ESP32 GND
    SE switch (CH9) high output      -> GPIO33, 3.3V when SE is ON
    L298N channel B ENB PWM          -> GPIO27
    L298N channel B IN3 / IN4        -> GPIO26 / GPIO25

  Do not connect bay power/VBAT directly to any ESP32 GPIO.
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
static constexpr uint8_t I2C_SDA = 21;
static constexpr uint8_t I2C_SCL = 22;
static constexpr uint8_t OLED_ADDR = 0x3C;
static constexpr uint8_t OLED_WIDTH = 128;
static constexpr uint8_t OLED_HEIGHT = 64;
static constexpr int8_t OLED_RESET = -1;
static constexpr uint8_t SERVO_PIN = 15;
static constexpr uint8_t SE_OUTPUT_PIN = 33; // 3.3V out while SE (CH9) is ON
static constexpr uint8_t SE_CRSF_CHANNEL = 8;
static constexpr uint8_t RY_PWM_PIN = 27;
static constexpr uint8_t RY_IN3_PIN = 26;
static constexpr uint8_t RY_IN4_PIN = 25;
static constexpr uint8_t RY_CRSF_CHANNEL = 1; // CH2: RJ Y
static constexpr uint8_t SERVO_LEDC_CHANNEL = 1;
static constexpr uint8_t SERVO_LEDC_RES_BITS = 16;
static constexpr uint16_t SERVO_PWM_HZ = 50;
static constexpr uint16_t SERVO_MIN_US = 500;
static constexpr uint16_t SERVO_MAX_US = 2500;
static constexpr uint8_t RY_LEDC_CHANNEL = 2;
static constexpr uint8_t RY_LEDC_RES_BITS = 10;
static constexpr uint16_t RY_PWM_HZ = 20000;
static constexpr float RY_STICK_DEADZONE = 0.02f;
static constexpr bool SERVO_SELF_TEST = false;
static constexpr uint32_t SERVO_SELF_TEST_STEP_MS = 1500;

static constexpr uint32_t SERIAL_MONITOR_BAUD = 115200;
static constexpr uint32_t MODULE_UART_BAUD = 420000; // CRSF default
static constexpr bool MODULE_UART_INVERTED = false;  // ELRS receiver TX is usually not inverted

static constexpr uint8_t MAX_CHANNELS = 16;
static constexpr uint16_t CRSF_RAW_MIN = 173;
static constexpr uint16_t CRSF_RAW_MAX = 1811;
static constexpr uint16_t PPM_MIN_US = 800;
static constexpr uint16_t PPM_MAX_US = 2400;
static constexpr uint16_t PPM_FRAME_GAP_US = 3500;
static constexpr uint32_t PRINT_INTERVAL_MS = 500;
static constexpr uint32_t DISPLAY_INTERVAL_MS = 150;
static constexpr uint32_t DISPLAY_PAGE_INTERVAL_MS = 2500;
static constexpr uint32_t CRSF_AUTOSCAN_INTERVAL_MS = 2000;
static constexpr uint32_t SIGNAL_TIMEOUT_MS = 1000;
static constexpr uint8_t POT_ACTION_DELTA_PERCENT = 2;
static constexpr bool PRINT_PPM_STATUS = false;
static constexpr bool PRINT_RAW_CHANNELS = false;
static constexpr bool AUTO_SCAN_CRSF_POLARITY = true;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;

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

const SerialProfileConfig *activeSerialProfile = &SERIAL_PROFILES[0];
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

static constexpr uint8_t ACTION_NONE = 0;
static constexpr uint8_t ACTION_LEFT_STICK = 1;
static constexpr uint8_t ACTION_RIGHT_STICK = 2;
static constexpr uint8_t ACTION_SA = 3;
static constexpr uint8_t ACTION_SB = 4;
static constexpr uint8_t ACTION_SC = 5;
static constexpr uint8_t ACTION_SD = 6;
static constexpr uint8_t ACTION_SE = 7;
static constexpr uint8_t ACTION_S1 = 8;
static constexpr uint8_t ACTION_AUX = 9;

struct LastAction {
    uint8_t type = ACTION_NONE;
    uint8_t channel = 0;
    char value[8] = "--";
    uint32_t updatedMs = 0;
};

LastAction lastAction;
uint16_t previousActionChannels[MAX_CHANNELS] = {};
bool previousActionReady = false;
uint16_t lastServoPulseUs = 0;
float currentServoAngleDeg = 90.0f;
uint16_t ryPwmDuty = 0;
int8_t ryMotorDirection = 0;

void updateServoFromRx();
void updateSeOutputFromRx();
void updateRyPwmFromRx();

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
    crsfChannels[0] = read11Bits(payload, 0);
    crsfFrameCounter++;
    crsfLastFrameMs = millis();
    if (!SERVO_SELF_TEST) {
        updateServoFromRx();
    }
    for (uint8_t ch = 1; ch < MAX_CHANNELS; ++ch) {
        crsfChannels[ch] = read11Bits(payload, ch);
    }
    if (!SERVO_SELF_TEST) {
        updateSeOutputFromRx();
        updateRyPwmFromRx();
    }
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
    ModuleSerial.setRxBufferSize(1024);
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
    const float value = (static_cast<float>(raw) - CRSF_RAW_MIN) / (CRSF_RAW_MAX - CRSF_RAW_MIN);
    return constrain(value, 0.0f, 1.0f);
}

float normalizeCrsfStick(uint16_t raw) {
    return -(normalizeCrsf01(raw) * 2.0f - 1.0f);
}

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

void printFloat2(float value) { Serial.print(value, 2); }

void printSignedFloat2(float value) {
    if (value >= 0.0f) {
        Serial.print('+');
    }
    Serial.print(value, 2);
}

int8_t stickPercent(uint16_t raw) {
    return static_cast<int8_t>(roundf(normalizeCrsfStick(raw) * 100.0f));
}

uint8_t potPercent(uint16_t raw) {
    return static_cast<uint8_t>(roundf(normalizeCrsf01(raw) * 100.0f));
}

uint8_t channelPercent(uint16_t raw) {
    return static_cast<uint8_t>(roundf(normalizeCrsf01(raw) * 100.0f));
}

const char *twoStateName(uint16_t raw) {
    return normalizeCrsfButton(raw) ? "ON" : "OFF";
}

char twoStateShort(uint16_t raw) {
    return normalizeCrsfButton(raw) ? '1' : '0';
}

const char *threeStateName(uint16_t raw) {
    switch (normalizeCrsfThreeState(raw)) {
    case 0:
        return "UP";
    case 1:
        return "MID";
    default:
        return "DN";
    }
}

char threeStateShort(uint16_t raw) {
    switch (normalizeCrsfThreeState(raw)) {
    case 0:
        return '0';
    case 1:
        return '1';
    default:
        return '2';
    }
}

void setLastAction(uint8_t type, uint8_t channel, const char *value) {
    lastAction.type = type;
    lastAction.channel = channel;
    strncpy(lastAction.value, value, sizeof(lastAction.value) - 1);
    lastAction.value[sizeof(lastAction.value) - 1] = '\0';
    lastAction.updatedMs = millis();
}

void setLastActionPercent(uint8_t type, uint8_t channel, uint8_t percent) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", percent);
    setLastAction(type, channel, buf);
}

void updateLastActionFromCrsf() {
    if (!previousActionReady) {
        for (uint8_t i = 0; i < MAX_CHANNELS; ++i) {
            previousActionChannels[i] = crsfChannels[i];
        }
        previousActionReady = true;
        setLastAction(ACTION_NONE, 0, "--");
        return;
    }

    if (twoStateShort(crsfChannels[4]) != twoStateShort(previousActionChannels[4])) {
        char value[] = {twoStateShort(crsfChannels[4]), '\0'};
        setLastAction(ACTION_SA, 5, value);
    }
    if (threeStateShort(crsfChannels[5]) != threeStateShort(previousActionChannels[5])) {
        char value[] = {threeStateShort(crsfChannels[5]), '\0'};
        setLastAction(ACTION_SB, 6, value);
    }
    if (threeStateShort(crsfChannels[6]) != threeStateShort(previousActionChannels[6])) {
        char value[] = {threeStateShort(crsfChannels[6]), '\0'};
        setLastAction(ACTION_SC, 7, value);
    }
    if (twoStateShort(crsfChannels[7]) != twoStateShort(previousActionChannels[7])) {
        char value[] = {twoStateShort(crsfChannels[7]), '\0'};
        setLastAction(ACTION_SD, 8, value);
    }
    if (twoStateShort(crsfChannels[8]) != twoStateShort(previousActionChannels[8])) {
        char value[] = {twoStateShort(crsfChannels[8]), '\0'};
        setLastAction(ACTION_SE, 9, value);
    }

    const uint8_t s1 = potPercent(crsfChannels[9]);
    const uint8_t previousS1 = potPercent(previousActionChannels[9]);
    if (abs(static_cast<int>(s1) - static_cast<int>(previousS1)) >= POT_ACTION_DELTA_PERCENT) {
        setLastActionPercent(ACTION_S1, 10, s1);
    }

    for (uint8_t ch = 10; ch < MAX_CHANNELS; ++ch) {
        const uint8_t current = channelPercent(crsfChannels[ch]);
        const uint8_t previous = channelPercent(previousActionChannels[ch]);
        if (abs(static_cast<int>(current) - static_cast<int>(previous)) >= POT_ACTION_DELTA_PERCENT) {
            setLastActionPercent(ACTION_AUX, ch + 1, current);
        }
    }

    for (uint8_t i = 0; i < MAX_CHANNELS; ++i) {
        previousActionChannels[i] = crsfChannels[i];
    }
}

void printPocketStateFromCrsf() {
    const float rjX = normalizeCrsfStick(crsfChannels[0]); // CH1: roll/aileron
    const float rjY = normalizeCrsfStick(crsfChannels[1]); // CH2: pitch/elevator
    const float ljY = normalizeCrsfStick(crsfChannels[2]); // CH3: throttle
    const float ljX = normalizeCrsfStick(crsfChannels[3]); // CH4: yaw/rudder

    // Pocket controls, assuming EdgeTX mixes CH5..CH10 to these physical controls.
    const uint8_t sa = normalizeCrsfButton(crsfChannels[4]);     // CH5
    const uint8_t sb = normalizeCrsfThreeState(crsfChannels[5]); // CH6
    const uint8_t sc = normalizeCrsfThreeState(crsfChannels[6]); // CH7
    const uint8_t sd = normalizeCrsfButton(crsfChannels[7]);     // CH8
    const uint8_t se = normalizeCrsfButton(crsfChannels[8]);     // CH9
    const float s1 = normalizeCrsf01(crsfChannels[9]);           // CH10
    Serial.print("LJ(X:");
    printSignedFloat2(ljX);
    Serial.print("|Y:");
    printSignedFloat2(ljY);
    Serial.print(") RJ(X:");
    printSignedFloat2(rjX);
    Serial.print("|Y:");
    printSignedFloat2(rjY);
    Serial.print(") SA:");
    Serial.print(sa);
    Serial.print(" SB:");
    Serial.print(sb);
    Serial.print(" SC:");
    Serial.print(sc);
    Serial.print(" SD:");
    Serial.print(sd);
    Serial.print(" SE:");
    Serial.print(se);
    Serial.print(" S1:");
    printFloat2(s1);
    Serial.println();
}

void showOledBootScreen() {
    if (!oledReady) {
        return;
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("RadioMaster Pocket");
    display.println("OLED + RX test");
    display.println();
    display.print("OLED SDA ");
    display.print(I2C_SDA);
    display.print(" SCL ");
    display.println(I2C_SCL);
    display.print("UART RX GPIO ");
    display.println(MODULE_RX_PIN);
    display.print("Mode ");
    display.println(activeSerialProfile->name);
    display.display();
}

void drawSignalHeader(const char *label, bool hasSignal) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(label);
    display.print(' ');
    display.print(activeSerialProfile->name);
    display.setCursor(100, 0);
    display.print(hasSignal ? "OK" : "NO");
    display.drawFastHLine(0, 9, OLED_WIDTH, SSD1306_WHITE);
}

void printSignedPercent(int8_t value) {
    if (value > 0) {
        display.print('+');
    }
    display.print(value);
}

void drawStickWidget(int16_t x, int16_t y, int16_t w, int16_t h, float stickX, float stickY,
                     const char *label) {
    display.drawRect(x, y, w, h, SSD1306_WHITE);
    display.drawLine(x + w / 2, y, x + w / 2, y + h - 1, SSD1306_WHITE);
    display.drawLine(x, y + h / 2, x + w - 1, y + h / 2, SSD1306_WHITE);

    const int16_t dotX = x + (w - 1) / 2 + static_cast<int16_t>(stickX * ((w - 6) / 2));
    const int16_t dotY = y + (h - 1) / 2 - static_cast<int16_t>(stickY * ((h - 6) / 2));
    display.fillCircle(dotX, dotY, 2, SSD1306_WHITE);
}

void drawStickAction(bool leftStick);

void drawCrsfLivePage() {
    drawStickAction(false);

    display.setTextSize(1);
    display.setCursor(0, 55);
    display.print("SA:");
    display.print(twoStateShort(crsfChannels[4]));
    display.print(" SB:");
    display.print(threeStateShort(crsfChannels[5]));
    display.print(" S1:");
    display.print(potPercent(crsfChannels[9]));
    display.print('%');
}

void drawCrsfOverviewPage() {
    drawCrsfLivePage();
}

void drawLargeTextCentered(const char *text, uint8_t textSize, int16_t y) {
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    display.setTextSize(textSize);
    display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
    const int16_t x = max<int16_t>(0, (OLED_WIDTH - static_cast<int16_t>(w)) / 2);
    display.setCursor(x, y);
    display.print(text);
    display.setTextSize(1);
}

const char *lastActionName() {
    switch (lastAction.type) {
    case ACTION_LEFT_STICK:
        return "LEFT STICK";
    case ACTION_RIGHT_STICK:
        return "RIGHT STICK";
    case ACTION_SA:
        return "SA";
    case ACTION_SB:
        return "SB";
    case ACTION_SC:
        return "SC";
    case ACTION_SD:
        return "SD";
    case ACTION_SE:
        return "SE";
    case ACTION_S1:
        return "S1";
    case ACTION_AUX:
        return "AUX";
    default:
        return "READY";
    }
}

void drawSwitchAction(const char *label, const char *value) {
    char line[16];
    snprintf(line, sizeof(line), "%s:%s", label, value);
    drawLargeTextCentered(line, 3, 20);
}

void drawPotAction(const char *label, const char *value) {
    char line[16];
    snprintf(line, sizeof(line), "%s:%s", label, value);
    drawLargeTextCentered(line, 3, 18);
}

void drawStickAction(bool leftStick) {
    const float stickX = leftStick ? normalizeCrsfStick(crsfChannels[3])
                                   : normalizeCrsfStick(crsfChannels[0]);
    const float stickY = leftStick ? normalizeCrsfStick(crsfChannels[2])
                                   : normalizeCrsfStick(crsfChannels[1]);
    const int8_t xPercent = leftStick ? stickPercent(crsfChannels[3])
                                      : stickPercent(crsfChannels[0]);
    const int8_t yPercent = leftStick ? stickPercent(crsfChannels[2])
                                      : stickPercent(crsfChannels[1]);

    drawStickWidget(2, 10, 38, 38, stickX, stickY, leftStick ? "LJ" : "RJ");

    display.setTextSize(2);
    display.setCursor(44, 11);
    display.print(leftStick ? "LX:" : "RX:");
    printSignedPercent(xPercent);
    display.setCursor(44, 35);
    display.print(leftStick ? "LY:" : "RY:");
    printSignedPercent(yPercent);
    display.setTextSize(1);
}

void drawLastActionPage() {
    switch (lastAction.type) {
    case ACTION_SA:
        drawSwitchAction("SA", lastAction.value);
        return;
    case ACTION_SB:
        drawSwitchAction("SB", lastAction.value);
        return;
    case ACTION_SC:
        drawSwitchAction("SC", lastAction.value);
        return;
    case ACTION_SD:
        drawSwitchAction("SD", lastAction.value);
        return;
    case ACTION_SE:
        drawSwitchAction("SE", lastAction.value);
        return;
    case ACTION_S1:
        drawPotAction("S1", lastAction.value);
        return;
    case ACTION_AUX: {
        char label[8];
        snprintf(label, sizeof(label), "CH%u", lastAction.channel);
        drawPotAction(label, lastAction.value);
        return;
    }
    default:
        drawCrsfLivePage();
        return;
    }
}

void drawRemoteSearchPage(uint32_t nowMs) {
    display.setCursor(0, 16);
    display.print("SEARCH");
    display.setCursor(0, 28);
    display.print("RX ");
    display.print(uartByteCounter);

    display.setCursor(0, 40);
    display.print("pk ");
    display.print(crsfPacketCounter);
    display.print(" crc ");
    display.print(crsfCrcFailCounter);

    display.setCursor(0, 52);
    display.print("Profile ");
    display.print(activeSerialProfile->name);
    display.print(" age ");
    display.print(crsfLastFrameMs == 0 ? 0 : nowMs - crsfLastFrameMs);
}

void drawCrsfSwitchPage() {
    display.setCursor(0, 12);
    display.print("SA ");
    display.print(twoStateName(crsfChannels[4]));
    display.print(" raw ");
    display.print(crsfChannels[4]);

    display.setCursor(0, 22);
    display.print("SB ");
    display.print(threeStateName(crsfChannels[5]));
    display.print(" raw ");
    display.print(crsfChannels[5]);

    display.setCursor(0, 32);
    display.print("SC ");
    display.print(threeStateName(crsfChannels[6]));
    display.print(" raw ");
    display.print(crsfChannels[6]);

    display.setCursor(0, 42);
    display.print("SD ");
    display.print(twoStateName(crsfChannels[7]));
    display.print(" SE ");
    display.print(twoStateName(crsfChannels[8]));

    display.setCursor(0, 52);
    display.print("S1 ");
    display.print(potPercent(crsfChannels[9]));
    display.print("% raw ");
    display.print(crsfChannels[9]);
}

void drawChannelCell(uint8_t row, uint8_t col, uint8_t chIndex) {
    const int16_t x = col == 0 ? 0 : 64;
    const int16_t y = 13 + row * 12;
    const uint8_t pct = channelPercent(crsfChannels[chIndex]);

    display.setCursor(x, y);
    display.print(chIndex + 1);
    display.print(' ');
    display.print(crsfChannels[chIndex]);
    display.print(' ');
    display.print(pct);
    display.print('%');
}

void drawCrsfChannelsPage(uint8_t firstChannel) {
    for (uint8_t row = 0; row < 4; ++row) {
        drawChannelCell(row, 0, firstChannel + row);
        drawChannelCell(row, 1, firstChannel + row + 4);
    }
}

void updateOledDisplay() {
    if (!oledReady) {
        return;
    }

    static uint32_t lastDisplayMs = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastDisplayMs < DISPLAY_INTERVAL_MS) {
        return;
    }
    lastDisplayMs = nowMs;

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setTextSize(1);

    if (isCrsfProfile()) {
        const bool hasSignal = crsfLastFrameMs != 0 && nowMs - crsfLastFrameMs <= SIGNAL_TIMEOUT_MS;

        if (hasSignal) {
            updateLastActionFromCrsf();
            switch (lastAction.type) {
            case ACTION_SA:
            case ACTION_SB:
            case ACTION_SC:
            case ACTION_SD:
            case ACTION_SE:
            case ACTION_S1:
            case ACTION_AUX:
                drawLastActionPage();
                break;
            default:
                drawCrsfLivePage();
                break;
            }
        } else {
            drawSignalHeader("CRSF", hasSignal);
            drawRemoteSearchPage(nowMs);
        }
    } else if (activeSerialProfile->profile == SerialProfile::Sbus) {
        const bool hasSignal = sbusLastFrameMs != 0 && nowMs - sbusLastFrameMs <= SIGNAL_TIMEOUT_MS;
        drawSignalHeader("SBUS", hasSignal);
        display.setCursor(0, 16);
        display.print("Frames ");
        display.println(sbusFrameCounter);
        display.print("Bad ");
        display.println(sbusBadFrameCounter);
        display.print("CH1 ");
        display.print(sbusChannels[0]);
        display.print(" CH2 ");
        display.println(sbusChannels[1]);
        display.print("UART ");
        display.println(uartByteCounter);
    } else if (activeSerialProfile->profile == SerialProfile::Ibus) {
        const bool hasSignal = ibusLastFrameMs != 0 && nowMs - ibusLastFrameMs <= SIGNAL_TIMEOUT_MS;
        drawSignalHeader("IBUS", hasSignal);
        display.setCursor(0, 16);
        display.print("Frames ");
        display.println(ibusFrameCounter);
        display.print("Bad ");
        display.println(ibusBadFrameCounter);
        display.print("CH1 ");
        display.print(ibusChannels[0]);
        display.print(" CH2 ");
        display.println(ibusChannels[1]);
        display.print("UART ");
        display.println(uartByteCounter);
    } else {
        drawSignalHeader("RAW", uartByteCounter > 0);
        display.setCursor(0, 16);
        display.print("UART bytes ");
        display.println(uartByteCounter);
        display.println("Use serial command:");
        display.println("crsf crsfinv sbus");
        display.println("ibus raw420 raw100");
    }

    display.display();
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
            printPocketStateFromCrsf();
        }
        lastCrsfCounter = crsfFrameCounter;
    } else if (crsfLastFrameMs == 0 || nowMs - crsfLastFrameMs > SIGNAL_TIMEOUT_MS) {
        Serial.print("CRSF: no RC frame");
        if (uartByteCounter != lastUartByteCounter) {
            Serial.print(" (UART bytes seen: ");
            Serial.print(uartByteCounter - lastUartByteCounter);
            Serial.print(')');
        }
        if (crsfPacketCounter != lastCrsfPacketCounter ||
            crsfCrcFailCounter != lastCrsfCrcFailCounter) {
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

void autoScanCrsfPolarity() {
    if (!AUTO_SCAN_CRSF_POLARITY || !isCrsfProfile()) {
        return;
    }

    const uint32_t nowMs = millis();
    if (crsfLastFrameMs != 0 && nowMs - crsfLastFrameMs <= SIGNAL_TIMEOUT_MS) {
        return;
    }

    static uint32_t lastScanMs = 0;
    static uint32_t lastScanUartBytes = 0;
    if (nowMs - lastScanMs < CRSF_AUTOSCAN_INTERVAL_MS) {
        return;
    }
    lastScanMs = nowMs;

    if (uartByteCounter == lastScanUartBytes) {
        return;
    }
    lastScanUartBytes = uartByteCounter;

    const uint8_t nextProfile =
        activeSerialProfile->profile == SerialProfile::Crsf ? 1 : 0;
    startModuleSerial(nextProfile);
    Serial.println("Auto-scan: switched CRSF polarity because UART bytes exist but no RC frame");
}

uint16_t servoPulseFromRx(uint16_t rxRaw) {
    const float stick = constrain(normalizeCrsfStick(rxRaw), -1.0f, 1.0f);
    const float pulse = (SERVO_MIN_US + SERVO_MAX_US) * 0.5f +
                        stick * (SERVO_MAX_US - SERVO_MIN_US) * 0.5f;
    return static_cast<uint16_t>(pulse + 0.5f);
}

float servoAngleFromRx(uint16_t rxRaw) {
    const float stick = constrain(normalizeCrsfStick(rxRaw), -1.0f, 1.0f);
    return (stick + 1.0f) * 90.0f;
}

uint32_t servoDutyFromPulseUs(uint16_t pulseUs) {
    static constexpr uint32_t maxDuty = (1UL << SERVO_LEDC_RES_BITS) - 1;
    static constexpr uint32_t periodUs = 1000000UL / SERVO_PWM_HZ;
    return (static_cast<uint32_t>(pulseUs) * maxDuty) / periodUs;
}

void servoWritePulseUs(uint16_t pulseUs) {
    lastServoPulseUs = constrain(pulseUs, SERVO_MIN_US, SERVO_MAX_US);
    ledcWrite(SERVO_LEDC_CHANNEL, servoDutyFromPulseUs(lastServoPulseUs));
}

void servoBegin() {
    pinMode(SERVO_PIN, OUTPUT);
    ledcSetup(SERVO_LEDC_CHANNEL, SERVO_PWM_HZ, SERVO_LEDC_RES_BITS);
    ledcAttachPin(SERVO_PIN, SERVO_LEDC_CHANNEL);
    servoWritePulseUs((SERVO_MIN_US + SERVO_MAX_US) / 2);
}

void updateServoFromRx() {
    currentServoAngleDeg = servoAngleFromRx(crsfChannels[0]);
    servoWritePulseUs(servoPulseFromRx(crsfChannels[0]));
}

void seOutputBegin() {
    pinMode(SE_OUTPUT_PIN, OUTPUT);
    digitalWrite(SE_OUTPUT_PIN, LOW);
}

void updateSeOutputFromRx() {
    const bool enabled = normalizeCrsfButton(crsfChannels[SE_CRSF_CHANNEL]) != 0;
    digitalWrite(SE_OUTPUT_PIN, enabled ? HIGH : LOW);
}

float ryStickFromRx(uint16_t raw) {
    const float stick = constrain(normalizeCrsfStick(raw), -1.0f, 1.0f);
    if (fabsf(stick) <= RY_STICK_DEADZONE) {
        return 0.0f;
    }
    return stick;
}

uint16_t ryDutyFromStick(float stick) {
    static constexpr uint16_t maxDuty = (1U << RY_LEDC_RES_BITS) - 1;
    return static_cast<uint16_t>(fabsf(stick) * maxDuty + 0.5f);
}

void ryPwmBegin() {
    pinMode(RY_IN3_PIN, OUTPUT);
    pinMode(RY_IN4_PIN, OUTPUT);
    pinMode(RY_PWM_PIN, OUTPUT);
    ledcSetup(RY_LEDC_CHANNEL, RY_PWM_HZ, RY_LEDC_RES_BITS);
    ledcAttachPin(RY_PWM_PIN, RY_LEDC_CHANNEL);
    digitalWrite(RY_IN3_PIN, LOW);
    digitalWrite(RY_IN4_PIN, LOW);
    ledcWrite(RY_LEDC_CHANNEL, 0);
    ryPwmDuty = 0;
    ryMotorDirection = 0;
}

void updateRyPwmFromRx() {
    const float stick = ryStickFromRx(crsfChannels[RY_CRSF_CHANNEL]);
    ryPwmDuty = ryDutyFromStick(stick);

    if (stick == 0.0f || ryPwmDuty == 0) {
        ryMotorDirection = 0;
        digitalWrite(RY_IN3_PIN, LOW);
        digitalWrite(RY_IN4_PIN, LOW);
        ledcWrite(RY_LEDC_CHANNEL, 0);
        return;
    }

    ryMotorDirection = stick > 0.0f ? 1 : -1;
    digitalWrite(RY_IN3_PIN, ryMotorDirection > 0 ? HIGH : LOW);
    digitalWrite(RY_IN4_PIN, ryMotorDirection < 0 ? HIGH : LOW);
    ledcWrite(RY_LEDC_CHANNEL, ryPwmDuty);
}

void updateServoSelfTest() {
    static uint8_t lastStep = 255;
    const uint8_t step = (millis() / SERVO_SELF_TEST_STEP_MS) % 4;
    if (step == lastStep) {
        return;
    }
    lastStep = step;

    uint16_t pulseUs = (SERVO_MIN_US + SERVO_MAX_US) / 2;
    const char *label = "CENTER";
    if (step == 0) {
        pulseUs = SERVO_MIN_US;
        label = "MIN";
    } else if (step == 1) {
        pulseUs = (SERVO_MIN_US + SERVO_MAX_US) / 2;
        label = "CENTER";
    } else if (step == 2) {
        pulseUs = SERVO_MAX_US;
        label = "MAX";
    }

    servoWritePulseUs(pulseUs);
    Serial.print("Servo self-test ");
    Serial.print(label);
    Serial.print(" pulse=");
    Serial.println(pulseUs);

    if (oledReady) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
        display.setCursor(0, 8);
        display.print("SERVO");
        display.setCursor(0, 30);
        display.print(label);
        display.setTextSize(1);
        display.setCursor(0, 54);
        display.print("GPIO15 ");
        display.print(pulseUs);
        display.print("us");
        display.display();
    }
}

void setup() {
    Serial.begin(SERIAL_MONITOR_BAUD);
    delay(300);

    Serial.println();
    Serial.println("RadioMaster Pocket external module bay reader");
    Serial.println("OLED: SDA=21 SCL=22 addr=0x3C");
    Serial.println("Servo: GPIO15 self-test MIN/CENTER/MAX enabled");
    Serial.println("SE out: GPIO33 = 3.3V when SE (CH9) is ON");
    Serial.print("L298N B: ENB PWM=GPIO");
    Serial.print(RY_PWM_PIN);
    Serial.print(" IN3=GPIO");
    Serial.print(RY_IN3_PIN);
    Serial.print(" IN4=GPIO");
    Serial.print(RY_IN4_PIN);
    Serial.print(" CH2 signed PWM deadzone=+/-");
    Serial.println(RY_STICK_DEADZONE, 2);
    Serial.println("PPM input: GPIO34, UART RX: GPIO16, GND common, GPIO max 3.3V");
    Serial.println("Commands: crsf, crsfinv, sbus, ibus, raw420, raw100, raw115");

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);
    oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    if (oledReady) {
        showOledBootScreen();
    } else {
        Serial.println("OLED not found at 0x3C. Try 0x3D or check SDA/SCL/VCC/GND.");
    }

    pinMode(PPM_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(PPM_PIN), ppmInterrupt, CHANGE);
    servoBegin();
    seOutputBegin();
    ryPwmBegin();

    ModuleSerial.setRxBufferSize(1024);
    ModuleSerial.begin(MODULE_UART_BAUD, SERIAL_8N1, MODULE_RX_PIN, MODULE_TX_PIN,
                       MODULE_UART_INVERTED);
    Serial.println("Default serial profile: crsf");
}

void loop() {
    pollCommands();
    pollSerialProtocol();

    if (SERVO_SELF_TEST) {
        updateServoSelfTest();
        pollSerialProtocol();
        return;
    }

    autoScanCrsfPolarity();

    // OLED and serial logging block the main loop; defer them while CRSF bytes are waiting.
    if (ModuleSerial.available() == 0) {
        printStatus();
        updateOledDisplay();
    }

    pollSerialProtocol();
}
