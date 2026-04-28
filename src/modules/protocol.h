#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "core/module.h"
#include "core/packets.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <ArduinoJson.h>
#pragma GCC diagnostic pop

/**
 * @brief Модуль JSON-протокола — «бюрократ» системы.
 * 
 * Принимает входящие JSON-команды от Transport, парсит их,
 * выполняет свою часть работы, остальные команды передаёт в шину.
 * Формирует исходящие JSON-ответы и фракционную телеметрию.
 */
class Protocol : public Module {
public:
    Protocol() : Module("Protocol")
        , engineOk(false), tripOk(false), klineOk(false), climateOk(false), settingsOk(false)
        , telemetryActive(false), transportOnline(false)
        , lastTelemetryMs(0), telemetryCounter(0) {}

protected:
    bool onInit() override;
    void onProcess() override;
    void onCommand(const CommandMsg& cmd) override;
    void onData(uint16_t topic, const void* data) override;

private:
    // === Кэш данных для телеметрии ===
    EnginePack  engine;     bool engineOk;
    TripPack    trip;       bool tripOk;
    KlinePack   kline;      bool klineOk;
    ClimatePack climate;    bool climateOk;
    SettingsPack settings;  bool settingsOk;

    // === Телеметрия ===
    bool telemetryActive;
    bool transportOnline;
    unsigned long lastTelemetryMs;
    int telemetryCounter;
    bool firstTelemetry = true;

    // === OTA ===
    int otaFirmwareSize = 0;
    bool _pendingOtaSuccess = false;
    bool _needVersionCheck = false;

    // === JSON-документы ===
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    StaticJsonDocument<768> inDoc;
    StaticJsonDocument<512> outDoc;
    #pragma GCC diagnostic pop

    // --- Диспетчер команд ---
    void processIncoming(const char* json);

    // --- Обработчики сложных команд ---
    void cmdSetCfg(const char* rawJson);
    void cmdOtaUpdate();
    void cmdOtaData();

    // --- Телеметрия ---
    void sendTelemetry();
    void buildFastJson();
    void addTripFields();
    void addServiceFields();

    // --- Отправка ---
    bool sendJson();
};

#endif