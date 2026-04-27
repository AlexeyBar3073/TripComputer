#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>
#include <string.h>

#define MSG_PAYLOAD_MAX 512

enum class MsgType : uint8_t {
    FAST = 0,
    FULL = 1
};

namespace Topic {
    constexpr uint16_t SYSTEM       = 1 << 0;
    constexpr uint16_t SENSOR       = 1 << 1;
    constexpr uint16_t PROTOCOL     = 1 << 2;
    constexpr uint16_t OTA          = 1 << 3;
    constexpr uint16_t STORAGE      = 1 << 4;
    constexpr uint16_t CALCULATOR   = 1 << 5;
    constexpr uint16_t ENGINE       = 1 << 6;
    constexpr uint16_t KLINE        = 1 << 7;
    constexpr uint16_t SERVICE      = 1 << 8;
    constexpr uint16_t TRANSPORT    = 1 << 9;
    constexpr uint16_t ALL          = 0xFFFF;
}

enum Command : uint8_t {
    CMD_NONE            = 0x00,

    // Управление trip/топливом (Calculator)
    CMD_RESET_TRIP_A    = 0x01,
    CMD_RESET_TRIP_B    = 0x02,
    CMD_RESET_AVG       = 0x03,
    CMD_FULL_TANK       = 0x04,
    CMD_CORRECT_ODO     = 0x05,

    // Настройки (Protocol)
    CMD_GET_CFG         = 0x06,
    CMD_SET_CFG         = 0x07,

    // K-Line диагностика
    CMD_KL_GET_DTC      = 0x08,
    CMD_KL_CLEAR_DTC    = 0x09,
    CMD_KL_RESET_ADAPT  = 0x0A,
    CMD_KL_PUMP_ATF     = 0x0B,
    CMD_KL_DETECT_PROTO = 0x0C,

    // Калибровка (Engine)
    CMD_CALIBRATE_SPEED    = 0x0D,
    CMD_CALIBRATE_INJECTOR = 0x0E,
    CMD_CALIBRATE_DEADTIME = 0x0F,

    // OTA — внешние (Android ↔ Protocol)
    CMD_OTA_END         = 0x10,
    CMD_OTA_START       = 0x11,

    // Транспорт
    CMD_TRANSPORT_STATUS = 0x12,

    // Телеметрия
    CMD_START_TELEMETRY  = 0x13,
    CMD_STOP_TELEMETRY   = 0x14,

    // OTA — внутренние (Protocol ↔ OTA Module)
    CMD_OTA_INIT        = 0x15,  // OTA → Protocol: готов, размер чанка
    CMD_OTA_WRITE       = 0x16,  // OTA → Protocol: чанк записан, номер
    CMD_OTA_UPDATE      = 0x17,  // Protocol → OTA: начать, размер прошивки
    CMD_OTA_DATA        = 0x18,  // Protocol → OTA: чанк данных (через Topic::OTA)
};

struct FastMsg {
    uint16_t topic;
    uint8_t  cmd;
    uint8_t  reserved;
    uint32_t timestamp;
    uint64_t value;
};

struct FullMsg {
    uint16_t topic;
    uint8_t  cmd;
    uint8_t  reserved;
    uint16_t msgId;
    uint16_t payloadSize;
    uint8_t  payload[MSG_PAYLOAD_MAX];
};

struct Message {
    MsgType type;
    
    union {
        FastMsg fast;
        FullMsg full;
    };
    
    Message() : type(MsgType::FAST) {
        memset(&fast, 0, sizeof(fast));
    }
    
    static Message createFast(uint16_t topic, uint8_t cmd, uint64_t value) {
        Message msg;
        msg.type = MsgType::FAST;
        msg.fast.topic = topic;
        msg.fast.cmd = cmd;
        msg.fast.reserved = 0;
        msg.fast.timestamp = 0;
        msg.fast.value = value;
        return msg;
    }
    
    static Message createFull(uint16_t topic, uint8_t cmd, 
                              const uint8_t* data, uint16_t size) {
        Message msg;
        msg.type = MsgType::FULL;
        msg.full.topic = topic;
        msg.full.cmd = cmd;
        msg.full.reserved = 0;
        msg.full.msgId = 0;
        msg.full.payloadSize = (size > MSG_PAYLOAD_MAX) ? MSG_PAYLOAD_MAX : size;
        if (data && msg.full.payloadSize > 0) {
            memcpy(msg.full.payload, data, msg.full.payloadSize);
        }
        return msg;
    }
};

#endif