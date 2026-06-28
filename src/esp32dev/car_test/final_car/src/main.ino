#include <Arduino.h>

// ---------------- Wiring ----------------
static constexpr uint8_t MOTOR_PWM_PIN = 13;
static constexpr uint8_t SERVO_PIN = 15;
static constexpr uint8_t ELRS_RX_PIN = 16; // ESP32 RX <- ELRS TX
static constexpr uint8_t ELRS_TX_PIN = 17; // ESP32 TX -> ELRS RX

// ---------------- CRSF ----------------
static constexpr uint32_t CRSF_BAUD = 420000;
static constexpr uint8_t CRSF_FRAME_RC_CHANNELS_PACKED = 0x16;
static constexpr uint8_t CRSF_MAX_FRAME_SIZE = 64;
static constexpr uint8_t CRSF_CHANNEL_COUNT = 16;
static constexpr uint16_t CRSF_CHANNEL_MIN = 172;
static constexpr uint16_t CRSF_CHANNEL_CENTER = 992;
static constexpr uint16_t CRSF_CHANNEL_MAX = 1811;
static constexpr uint8_t STEERING_CHANNEL = 0; // CH1, right stick X (RX)
static constexpr uint8_t THROTTLE_CHANNEL = 1; // CH2, right stick Y (RY)
static constexpr uint32_t FAILSAFE_TIMEOUT_MS = 500;

// ---------------- Motor PWM ----------------
static constexpr uint8_t MOTOR_PWM_CHANNEL = 0;
static constexpr uint32_t MOTOR_PWM_FREQUENCY_HZ = 20000;
static constexpr uint8_t MOTOR_PWM_RESOLUTION_BITS = 10;
static constexpr uint16_t MOTOR_PWM_MAX_DUTY =
    (1U << MOTOR_PWM_RESOLUTION_BITS) - 1;
static constexpr float THROTTLE_DEADZONE = 0.02f;

// ---------------- Servo PWM ----------------
static constexpr uint8_t SERVO_PWM_CHANNEL = 1;
static constexpr uint16_t SERVO_PWM_FREQUENCY_HZ = 50;
static constexpr uint8_t SERVO_PWM_RESOLUTION_BITS = 16;
static constexpr uint32_t SERVO_PWM_MAX_DUTY =
    (1UL << SERVO_PWM_RESOLUTION_BITS) - 1;
static constexpr uint32_t SERVO_PWM_PERIOD_US =
    1000000UL / SERVO_PWM_FREQUENCY_HZ;
static constexpr uint16_t SERVO_MIN_PULSE_US = 500;
static constexpr uint16_t SERVO_MAX_PULSE_US = 2500;
static constexpr float SERVO_LEFT_ANGLE_DEG = 85.0f;
static constexpr float SERVO_CENTER_ANGLE_DEG = 90.0f;
static constexpr float SERVO_RIGHT_ANGLE_DEG = 95.0f;

static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint32_t PRINT_INTERVAL_MS = 100;

HardwareSerial ElrsSerial(2);

uint8_t crsfFrame[CRSF_MAX_FRAME_SIZE] = {};
uint8_t crsfPosition = 0;
uint8_t crsfExpectedSize = 0;
uint16_t channels[CRSF_CHANNEL_COUNT] = {};
uint32_t validFrameCount = 0;
uint32_t crcErrorCount = 0;
uint32_t lastValidFrameMs = 0;
bool signalActive = false;
bool failsafeApplied = false;
float currentRx = 0.0f;
float currentRy = 0.0f;
float currentServoAngle = SERVO_CENTER_ANGLE_DEG;
uint16_t currentMotorDuty = 0;

uint8_t crc8D5(const uint8_t *data, uint8_t length) {
    uint8_t crc = 0;
    while (length-- > 0) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U)
                      ? static_cast<uint8_t>((crc << 1U) ^ 0xD5U)
                      : static_cast<uint8_t>(crc << 1U);
        }
    }
    return crc;
}

uint16_t readCrsfChannel(const uint8_t *payload, uint8_t channel) {
    const uint16_t bitOffset = static_cast<uint16_t>(channel) * 11U;
    const uint8_t byteOffset = bitOffset / 8U;
    const uint8_t shift = bitOffset % 8U;
    uint32_t value = static_cast<uint32_t>(payload[byteOffset]);
    if (byteOffset + 1U < 22U) {
        value |= static_cast<uint32_t>(payload[byteOffset + 1U]) << 8U;
    }
    if (byteOffset + 2U < 22U) {
        value |= static_cast<uint32_t>(payload[byteOffset + 2U]) << 16U;
    }
    return static_cast<uint16_t>((value >> shift) & 0x07FFU);
}

float normalizeCrsfChannel(uint16_t raw) {
    float value;
    if (raw >= CRSF_CHANNEL_CENTER) {
        value = static_cast<float>(raw - CRSF_CHANNEL_CENTER) /
                static_cast<float>(CRSF_CHANNEL_MAX - CRSF_CHANNEL_CENTER);
    } else {
        value = -static_cast<float>(CRSF_CHANNEL_CENTER - raw) /
                static_cast<float>(CRSF_CHANNEL_CENTER - CRSF_CHANNEL_MIN);
    }
    return constrain(value, -1.0f, 1.0f);
}

uint32_t servoPulseToDuty(uint16_t pulseUs) {
    return static_cast<uint32_t>(pulseUs) * SERVO_PWM_MAX_DUTY /
           SERVO_PWM_PERIOD_US;
}

uint16_t servoAngleToPulse(float angleDeg) {
    const float angle = constrain(angleDeg, 0.0f, 180.0f);
    return static_cast<uint16_t>(
        SERVO_MIN_PULSE_US +
        angle * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) / 180.0f);
}

void setServoAngle(float angleDeg) {
    currentServoAngle = constrain(angleDeg,
                                  SERVO_LEFT_ANGLE_DEG,
                                  SERVO_RIGHT_ANGLE_DEG);
    ledcWrite(SERVO_PWM_CHANNEL,
              servoPulseToDuty(servoAngleToPulse(currentServoAngle)));
}

void setMotorThrottle(float throttle) {
    // Reverse RY values do not reverse the motor: they command stop.
    const float forward = (throttle > THROTTLE_DEADZONE)
                              ? constrain(throttle, 0.0f, 1.0f)
                              : 0.0f;
    currentMotorDuty = static_cast<uint16_t>(
        forward * static_cast<float>(MOTOR_PWM_MAX_DUTY) + 0.5f);
    ledcWrite(MOTOR_PWM_CHANNEL, currentMotorDuty);
}

void applyControls() {
    currentRx = normalizeCrsfChannel(channels[STEERING_CHANNEL]);
    currentRy = normalizeCrsfChannel(channels[THROTTLE_CHANNEL]);

    const float servoAngle = SERVO_CENTER_ANGLE_DEG - currentRx * 5.0f;
    setServoAngle(servoAngle);
    setMotorThrottle(currentRy);
    signalActive = true;
    failsafeApplied = false;
}

void applyFailsafe() {
    currentRx = 0.0f;
    currentRy = 0.0f;
    setMotorThrottle(0.0f);
    setServoAngle(SERVO_CENTER_ANGLE_DEG);
    signalActive = false;
    failsafeApplied = true;
}

void processCrsfFrame(const uint8_t *frame, uint8_t frameSize) {
    if (frameSize < 4) {
        return;
    }

    const uint8_t length = frame[1];
    const uint8_t receivedCrc = frame[frameSize - 1];
    const uint8_t calculatedCrc = crc8D5(&frame[2], length - 1);
    if (receivedCrc != calculatedCrc) {
        ++crcErrorCount;
        return;
    }

    if (frame[2] != CRSF_FRAME_RC_CHANNELS_PACKED || length != 24) {
        return;
    }

    const uint8_t *payload = &frame[3];
    for (uint8_t channel = 0; channel < CRSF_CHANNEL_COUNT; ++channel) {
        channels[channel] = readCrsfChannel(payload, channel);
    }

    ++validFrameCount;
    lastValidFrameMs = millis();
    applyControls();
}

void readElrs() {
    while (ElrsSerial.available() > 0) {
        const uint8_t byte = static_cast<uint8_t>(ElrsSerial.read());

        if (crsfPosition == 0) {
            crsfFrame[crsfPosition++] = byte; // Device address
            continue;
        }

        if (crsfPosition == 1) {
            if (byte < 2 || byte > CRSF_MAX_FRAME_SIZE - 2) {
                crsfPosition = 0;
                crsfExpectedSize = 0;
                continue;
            }
            crsfFrame[crsfPosition++] = byte;
            crsfExpectedSize = byte + 2;
            continue;
        }

        crsfFrame[crsfPosition++] = byte;
        if (crsfPosition == crsfExpectedSize) {
            processCrsfFrame(crsfFrame, crsfExpectedSize);
            crsfPosition = 0;
            crsfExpectedSize = 0;
        }
    }
}

void printStatus() {
    static uint32_t nextPrintMs = 0;
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - nextPrintMs) < 0) {
        return;
    }
    nextPrintMs = now + PRINT_INTERVAL_MS;

    const float motorPercent =
        100.0f * currentMotorDuty / static_cast<float>(MOTOR_PWM_MAX_DUTY);
    Serial.printf("signal:%s RX:%+.3f RY:%+.3f servo:%.1fdeg motor:%.1f%% frames:%lu crc_err:%lu\n",
                  signalActive ? "OK" : "FAILSAFE",
                  currentRx,
                  currentRy,
                  currentServoAngle,
                  motorPercent,
                  static_cast<unsigned long>(validFrameCount),
                  static_cast<unsigned long>(crcErrorCount));
}

void setup() {
    Serial.begin(SERIAL_BAUD);

    // Initialize outputs immediately in their safe state.
    ledcSetup(MOTOR_PWM_CHANNEL,
              MOTOR_PWM_FREQUENCY_HZ,
              MOTOR_PWM_RESOLUTION_BITS);
    ledcAttachPin(MOTOR_PWM_PIN, MOTOR_PWM_CHANNEL);
    ledcWrite(MOTOR_PWM_CHANNEL, 0);

    ledcSetup(SERVO_PWM_CHANNEL,
              SERVO_PWM_FREQUENCY_HZ,
              SERVO_PWM_RESOLUTION_BITS);
    ledcAttachPin(SERVO_PIN, SERVO_PWM_CHANNEL);
    setServoAngle(SERVO_CENTER_ANGLE_DEG);

    ElrsSerial.begin(CRSF_BAUD,
                     SERIAL_8N1,
                     ELRS_RX_PIN,
                     ELRS_TX_PIN,
                     false);

    applyFailsafe();
    Serial.println("Final car controller started");
    Serial.println("CH1/RX -> servo 85..95 deg; CH2/RY positive -> motor 0..100%");
}

void loop() {
    readElrs();

    const uint32_t now = millis();
    if (!failsafeApplied && now - lastValidFrameMs > FAILSAFE_TIMEOUT_MS) {
        applyFailsafe();
    }

    printStatus();
}
