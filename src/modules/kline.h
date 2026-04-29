/**
 * @file kline.h
 * @brief Модуль диагностики по шине K-Line (ISO 9141-2).
 * 
 * Аппаратная основа:
 * - MC33290 Level Shifter
 * - UART2: TX=GPIO17, RX=GPIO16
 * - Скорость: 10400 бод, 8N1
 * 
 * Протокол:
 * - Инициализация: 5-baud slow init (адрес 0x33)
 * - Адреса ЭБУ: ECM=0x7E0, TCM=0x7E1
 * - Запрос данных: Mode 01 (стандартный OBD2), Mode 21 (Toyota extended)
 */

#ifndef KLINE_H
#define KLINE_H

#include "core/module.h"
#include "core/packets.h"
#include <HardwareSerial.h>

// Пины UART2
#define KLINE_TX 17
#define KLINE_RX 16

// Адреса ЭБУ (12-битные, передаются в sendFrame по частям)
#define KLINE_ECU_ECM  0x7E0
#define KLINE_ECU_TCM  0x7E1

// Таймауты ISO 9141-2 (мс)
#define KLINE_W1_TIMEOUT   25
#define KLINE_W4_TIMEOUT  300
#define KLINE_INIT_RETRIES  3

// Размеры буферов
#define KLINE_RX_BUF  256
#define KLINE_TX_BUF  64

enum class KlineState : uint8_t {
    IDLE, WAIT_POWER_ON, SEND_ADDR_LOW, SEND_ADDR_BITS,
    WAIT_SYNC, WAIT_KEY_BYTE1, WAIT_KEY_BYTE2, WAIT_ECU_ID,
    CONNECTED, FAILED
};

class KLine : public Module {
public:
    KLine() : Module("KLine") {}

protected:
    bool onInit() override;
    void onProcess() override;
    void onCommand(const CommandMsg& cmd) override;

private:
    KlinePack pack;
    unsigned long lastPublish = 0;

    bool _realMode = true;
    bool _connected = false;
    HardwareSerial* _uart = nullptr;
    
    KlineState _state = KlineState::IDLE;
    unsigned long _stateTimer = 0;
    uint8_t _retries = 0;
    uint8_t _addrBitIdx = 0;
    uint8_t _ecuId = 0;
    
    struct {
        bool readDtc    : 1;
        bool clearDtc   : 1;
        bool resetTcm   : 1;
        bool pumpAbs    : 1;
        bool detectProto: 1;
    } _cmd;
    
    uint8_t _rxBuf[KLINE_RX_BUF];
    uint8_t _txBuf[KLINE_TX_BUF];
    uint16_t _rxLen = 0;

    void uartInit();
    void startInit();
    void processInit();
    
    void txByte(uint8_t b);
    uint8_t rxByte(unsigned long timeoutMs);
    void sendFrame(uint16_t ecu, uint8_t mode, uint16_t pid);
    static uint8_t checksum(const uint8_t* data, int len);
    int readFrame(unsigned long timeoutMs);
    
    float readCoolantTemp();
    float readAtfTemp();
    float readVoltage();
    float readFuelPercent();
    float readOutputShaftRpm();
    int readDtcCodes();
    bool clearDtc();
    bool readGear();
    void resetTcmAdaptation();
    bool fastInit();
    
    void simulateData();
};

#endif