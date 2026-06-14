#include "config.h"

namespace {

HardwareSerial CrsfSerial(2);

constexpr uint8_t CRSF_FRAMETYPE_RC_CHANNELS_PACKED = 0x16;
constexpr uint8_t CRSF_MAX_PACKET_SIZE = 64;

uint8_t crsfBuf[CRSF_MAX_PACKET_SIZE] = {};
uint8_t crsfPos = 0;
uint8_t crsfExpected = 0;

uint16_t crsfChannels[MAX_CHANNELS] = {};
uint32_t crsfLastFrameMs = 0;
PocketState pocketState;

bool isCrsfAddress(uint8_t value) {
    switch (value) {
    case 0xC8:
    case 0xEA:
    case 0xEC:
    case 0xEE:
    case 0x00:
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
    pocketState.rjX = normalizeCrsfStick(crsfChannels[0]);
    pocketState.rjY = normalizeCrsfStick(crsfChannels[1]);
    pocketState.ljY = normalizeCrsfStick(crsfChannels[2]);
    pocketState.ljX = normalizeCrsfStick(crsfChannels[3]);
    pocketState.sa = normalizeCrsfButton(crsfChannels[4]);
    pocketState.sb = normalizeCrsfThreeState(crsfChannels[5]);
    pocketState.sc = normalizeCrsfThreeState(crsfChannels[6]);
    pocketState.sd = normalizeCrsfButton(crsfChannels[7]);
    pocketState.se = normalizeCrsfButton(crsfChannels[8]);
    pocketState.s1 = normalizeCrsf01(crsfChannels[9]);
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

} // namespace

void elrsBegin() {
    CrsfSerial.begin(CRSF_BAUD, SERIAL_8N1, CRSF_RX_PIN, CRSF_TX_PIN, CRSF_UART_INVERTED);
}

void elrsPoll() {
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

const PocketState &elrsGetState() { return pocketState; }
