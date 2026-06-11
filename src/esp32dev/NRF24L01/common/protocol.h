#pragma once
#include <Arduino.h>

// Команды протокола между двумя узлами.
enum : uint8_t {
    CMD_BLINK = 0x01,  // попроси соседа моргнуть
    CMD_ACK   = 0x02,  // подтверждение «моргнул»
    CMD_PING  = 0x03,  // heartbeat
};

struct Packet {
    uint8_t  cmd;        // команда из enum выше
    uint8_t  fromId;     // 1 = node-a, 2 = node-b
    uint16_t blinkMs;    // длительность одного мигания
    uint8_t  blinkCount; // количество миганий
    uint32_t seq;        // счётчик отправок
    uint32_t uptimeMs;   // millis() отправителя
} __attribute__((packed));

// Радио настройки общие для обоих узлов.
constexpr uint8_t  RF_CHANNEL  = 76;
constexpr uint32_t SEND_PERIOD = 2000;  // период автоотправки команды «мигни»

// Пайпы: каждый узел читает свой, пишет в соседский.
constexpr uint8_t PIPE_A[6] = "NODEA";
constexpr uint8_t PIPE_B[6] = "NODEB";
