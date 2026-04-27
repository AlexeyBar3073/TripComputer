/**
 * @file protocol.h
 * @brief Модуль обработки JSON-протокола обмена данными.
 * 
 * Класс Protocol реализует двусторонний JSON-протокол для:
 * - Приёма и разбора команд от клиента
 * - Формирования и отправки телеметрии
 * - Обработки запросов настроек
 * 
 * Работает как мост между транспортом (Bluetooth) и внутренней шиной.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "core/module.h"
#include "core/packets.h"

/**
 * Подавление предупреждения о deprecated-методах ArduinoJson
 * (используем V6 API для совместимости)
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <ArduinoJson.h>
#pragma GCC diagnostic pop

/**
 * @class Protocol
 * @brief Модуль JSON-протокола обмена данными.
 * 
 * Обрабатывает входящие JSON-команды и формирует ответы.
 * Управляет потоком телеметрии по требованию.
 */
class Protocol : public Module {
public:
    /**
     * @brief Конструктор модуля.
     * 
     * Устанавливает имя "Protocol" и инициализирует флаги состояния.
     */
    Protocol() : Module("Protocol")
        , engineOk(false), tripOk(false), klineOk(false), climateOk(false), settingsOk(false)
        , settingsChanged(false)
        , telemetryActive(false), transportOnline(false)
        , lastTelemetryMs(0), telemetryCounter(0) {}

protected:
    /**
     * @brief Инициализация модуля.
     * 
     * Подписывается на несколько топиков для сбора данных:
     * - Topic::TRANSPORT  — входящие JSON-команды
     * - Topic::SENSOR     — данные с датчиков
     * - Topic::CALCULATOR — данные о поездке
     * - Topic::KLINE      — данные K-Line
     * - Topic::SERVICE    — данные климат-системы
     * - Topic::STORAGE    — настройки
     * 
     * @return true — всегда успешна
     */
    bool onInit() override;

    /**
     * @brief Периодическая отправка телеметрии.
     * 
     * Если телеметрия активна и транспорт онлайн — отправляет
     * пакет данных каждые 150 мс.
     */
    void onProcess() override;

    /**
     * @brief Обработка команд.
     * 
     * Реагирует на CMD_TRANSPORT_STATUS — изменение состояния подключения.
     * 
     * @param cmd Команда от системы
     */
    void onCommand(const CommandMsg& cmd) override;

    /**
     * @brief Обработка входящих данных.
     * 
     * Собирает данные из всех топиков для формирования телеметрии.
     * 
     * @param topic Идентификатор топика
     * @param data  Указатель на данные
     */
    void onData(uint16_t topic, const void* data) override;

private:
    // === Кэш данных ===
    EnginePack  engine;     ///< Последние данные с датчиков
    TripPack    trip;       ///< Последние данные о поездке
    KlinePack   kline;      ///< Последние данные K-Line
    ClimatePack climate;    ///< Последние данные климат-системы
    SettingsPack settings;  ///< Последние настройки

    // === Флаги состояния ===
    bool engineOk, tripOk, klineOk, climateOk, settingsOk; ///< Флаги: данные загружены
    bool settingsChanged;   ///< Флаг: настройки изменены, нужно отправить

    // === Управление телеметрией ===
    bool telemetryActive;   ///< Флаг: телеметрия активна
    bool transportOnline;   ///< Флаг: Bluetooth подключён
    unsigned long lastTelemetryMs; ///< Время последней отправки телеметрии
    int telemetryCounter;   ///< Счётчик пакетов телеметрии
    bool firstTelemetry;    ///< Флаг: первая отправка (все поля)

    // === Обновление прошивки ===
    int otaFirmwareSize = 0;

    // === JSON-документы ===
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    StaticJsonDocument<768> inDoc;  ///< Входящий JSON
    StaticJsonDocument<512> outDoc; ///< Исходящий JSON
    #pragma GCC diagnostic pop

    // === Внутренние методы ===
    
    /**
     * @brief Обработка входящей JSON-команды.
     * 
     * Разбирает JSON, определяет команду и выполняет соответствующее действие.
     * 
     * @param json Указатель на строку с JSON
     */
    void processIncoming(const char* json);

    /**
     * @brief Формирование ответа на get_cfg.
     * 
     * Заполняет outDoc полями из settings или значениями по умолчанию.
     */
    void buildCfgResponse();

    /**
     * @brief Обработка команды set_cfg.
     * 
     * Обновляет локальные настройки из поля "data" входящего JSON.
     */
    void handleSetCfg();

    /**
     * @brief Отправка телеметрии.
     * 
     * Формирует и отправляет пакет телеметрии, если разрешено.
     */
    void sendTelemetry();

    /**
     * @brief Формирование базового пакета телеметрии.
     * 
     * Заполняет поля, обновляемые каждые 150 мс.
     */
    void buildFastJson();

    /**
     * @brief Добавление полей поездки.
     * 
     * Добавляет поля trip_a, fuel_a, avg и др. — обновляется каждые 600 мс.
     */
    void addTripFields();

    /**
     * @brief Добавление сервисных полей.
     * 
     * Добавляет поля coolant_temp, dtc, tire и др. — обновляется каждые 1.5 с.
     */
    void addServiceFields();

    /**
     * @brief Отправка JSON-пакета.
     * 
     * Сериализует outDoc и публикует в Topic::PROTOCOL.
     * 
     * @return true при успехе, false при ошибке
     */
    bool sendJson();
};

#endif