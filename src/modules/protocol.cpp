/**
 * @file protocol.cpp
 * @brief Реализация JSON-протокола обмена данными.
 * 
 * Обрабатывает входящие команды и формирует ответы в формате JSON.
 * Управляет потоком телеметрии по требованию.
 */

#include "protocol.h"
#include "core/message.h"
#include "core/logging.h"
#include "core/ota_chunk.h"
#include "core/version.h"

// ---------- Таблица всех поддерживаемых команд ----------

/**
 * @struct CmdEntry
 * @brief Связка имени команды и её кода.
 * 
 * Используется для поиска команды по строковому имени.
 */
struct CmdEntry {
    const char* name;   ///< Имя команды в JSON (например, "get_cfg")
    uint8_t     code;   ///< Соответствующий код команды (enum Command)
};

/**
 * @brief Таблица всех поддерживаемых команд.
 * 
 * Сопоставляет строковые имена команд с их числовыми кодами.
 * Используется в findCommand() для парсинга.
 */
static const CmdEntry CMD_TABLE[] = {
    {"get_cfg",             CMD_GET_CFG},
    {"set_cfg",             CMD_SET_CFG},
    {"reset_trip_a",        CMD_RESET_TRIP_A},
    {"reset_trip_b",        CMD_RESET_TRIP_B},
    {"reset_avg",           CMD_RESET_AVG},
    {"full_tank",           CMD_FULL_TANK},
    {"correct_odo",         CMD_CORRECT_ODO},
    {"start_telemetry",     CMD_START_TELEMETRY},
    {"stop_telemetry",      CMD_STOP_TELEMETRY},
    {"kl_get_dtc",          CMD_KL_GET_DTC},
    {"kl_clear_dtc",        CMD_KL_CLEAR_DTC},
    {"kl_reset_adapt",      CMD_KL_RESET_ADAPT},
    {"kl_pump_atf",         CMD_KL_PUMP_ATF},
    {"kl_detect_protocol",  CMD_KL_DETECT_PROTO},
    {"ota_update",          CMD_OTA_UPDATE},
    {"ota_data",            CMD_OTA_DATA},
    {"ota_end",             CMD_OTA_END},
};

/**
 * @brief Поиск кода команды по её имени.
 * 
 * Проходит по таблице CMD_TABLE и возвращает код команды.
 * 
 * @param name Указатель на строку с именем команды
 * @return Код команды, или CMD_NONE если не найдена
 */
static uint8_t findCommand(const char* name) {
    for (const auto& e : CMD_TABLE) {
        if (strcmp(name, e.name) == 0) return e.code;
    }
    return CMD_NONE;
}

// ---------- Реализация Protocol ----------

/**
 * @brief Инициализация модуля Protocol.
 * 
 * Подписывается на:
 * - Topic::TRANSPORT  — входящие JSON-команды
 * - Topic::SENSOR     — данные с датчиков
 * - Topic::CALCULATOR — данные о пробеге и расходе
 * - Topic::KLINE      — данные K-Line
 * - Topic::SERVICE    — данные климат-системы
 * - Topic::STORAGE    — настройки устройства
 * 
 * @return true — всегда успешна
 */
bool Protocol::onInit() {
    subscribeNew(Topic::TRANSPORT, 512);
    subscribeNew(Topic::SENSOR,     sizeof(EnginePack));
    subscribeNew(Topic::CALCULATOR, sizeof(TripPack));
    subscribeNew(Topic::KLINE,      sizeof(KlinePack));
    subscribeNew(Topic::SERVICE,    sizeof(ClimatePack));
    subscribeNew(Topic::STORAGE,    sizeof(SettingsPack));

    _needVersionCheck = true;
    return true;
}

/**
 * @brief Обработка команд.
 * 
 * Реагирует на CMD_TRANSPORT_STATUS — изменение состояния подключения.
 * 
 * @param cmd Команда от системы
 */
void Protocol::onCommand(const CommandMsg& cmd) {
    switch (cmd.cmd) {
        case CMD_TRANSPORT_STATUS:
            transportOnline = (cmd.value != 0);
            LOG_INFO(name, "Transport %s", transportOnline ? "ONLINE" : "OFFLINE");
            
            if (transportOnline && _pendingOtaSuccess) {
                _pendingOtaSuccess = false;
                outDoc.clear();
                outDoc["ota_success"] = true;
                outDoc["version"] = FW_VERSION_STR;
                sendJson();
            }
            break;
            
        case CMD_OTA_INIT: {
            uint16_t cs = (uint16_t)cmd.value;
            int total = (otaFirmwareSize + cs - 1) / cs;
            LOG_INFO(name, "OTA init: chunk=%d, total=%d", cs, total);
            
            outDoc.clear();
            JsonObject ota = outDoc["ota_init"].to<JsonObject>();
            ota["size"] = cs;
            ota["count"] = total;
            sendJson();
            break;
        }
        
        case CMD_OTA_WRITE: {
            int pack = (int)cmd.value;
            outDoc.clear();
            if (pack > 0) {
                outDoc["ota_read"] = pack;
            } else {
                JsonObject replay = outDoc["ota_replay"].to<JsonObject>();
                replay["pack"] = -pack;
            }
            sendJson();
            break;
        }
        
        case CMD_OTA_RESTART:
            outDoc.clear();
            outDoc["ota_restart"] = 1;
            sendJson();
            break;
    }
}

/**
 * @brief Обработка входящих данных.
 * 
 * Копирует данные из топиков в локальные переменные и устанавливает флаги.
 * 
 * @param topic Идентификатор топика
 * @param data  Указатель на данные
 */
void Protocol::onData(uint16_t topic, const void* data) {
    switch (topic) {
        case Topic::TRANSPORT:
            processIncoming((const char*)data);
            break;
            
        case Topic::SENSOR:
            memcpy(&engine, data, sizeof(EnginePack));
            engineOk = true;
            break;
            
        case Topic::CALCULATOR:
            memcpy(&trip, data, sizeof(TripPack));
            tripOk = true;
            break;
            
        case Topic::KLINE:
            memcpy(&kline, data, sizeof(KlinePack));
            klineOk = true;
            break;
            
        case Topic::SERVICE:
            memcpy(&climate, data, sizeof(ClimatePack));
            climateOk = true;
            break;
            
        case Topic::STORAGE:
            memcpy(&settings, data, sizeof(SettingsPack));
            settingsOk = true;
            LOG_INFO(name, "Settings: tank=%.1f", settings.tank_capacity);
            
            if (_needVersionCheck) {
                _needVersionCheck = false;
                if (strcmp(settings.fw_version, FW_VERSION_STR) != 0 && settings.fw_version[0] != '\0') {
                    LOG_INFO(name, "OTA success: %s -> %s", settings.fw_version, FW_VERSION_STR);
                    _pendingOtaSuccess = true;
                    if (transportOnline) {
                        _pendingOtaSuccess = false;
                        outDoc.clear();
                        outDoc["ota_success"] = true;
                        outDoc["version"] = FW_VERSION_STR;
                        sendJson();
                    }
                }
                strncpy(settings.fw_version, FW_VERSION_STR, sizeof(settings.fw_version));
                publish(Topic::STORAGE, &settings, sizeof(SettingsPack));
            }
            break;
    }
}

/**
 * @brief Периодическая обработка.
 * 
 * Вызывает sendTelemetry() для отправки данных, если разрешено.
 */
void Protocol::onProcess() {
    if (_pendingOtaSuccess && transportOnline) {
        LOG_INFO(name, "Sending OTA success...");
        _pendingOtaSuccess = false;
        outDoc.clear();
        outDoc["ota_success"] = true;
        outDoc["version"] = FW_VERSION_STR;
        sendJson();
    }
    
    sendTelemetry();
}

// ---------- Обработка входящих команд ----------

/**
 * @brief Обработка входящей JSON-команды.
 * 
 * Разбирает JSON, определяет команду и выполняет соответствующее действие.
 * Формирует ответ (ack_id) и отправляет его.
 * 
 * @param json Указатель на строку с JSON-командой
 */
void Protocol::processIncoming(const char* json) {
    inDoc.clear();
    DeserializationError err = deserializeJson(inDoc, json);
    
    const char* cmdStr = inDoc["command"] | inDoc["cmd"];
    int inMsgId = inDoc["msg_id"] | 0;
    
    outDoc.clear();
    
    if (err || !cmdStr) {
        if (inMsgId > 0) outDoc["ack_id"] = inMsgId;
        sendJson();
        return;
    }
    
    LOG_INFO(name, "Cmd: %s, id=%d", cmdStr, inMsgId);
    
    Command cmd = (Command)findCommand(cmdStr);
    
    switch (cmd) {
        case CMD_GET_CFG:
            buildCfgResponse();
            break;
            
        case CMD_SET_CFG: {
            JsonObject in = inDoc["data"];
            if (in) {
                SettingsPack cfg = settings;
                if (in["tV"])    cfg.tank_capacity    = in["tV"];
                if (in["iPerf"]) cfg.injector_flow    = in["iPerf"];
                if (in["iCnt"])  cfg.injector_count   = in["iCnt"];
                if (in["sSig"])  cfg.pulses_per_meter = in["sSig"];
                if (in["kPrt"])  cfg.kline_protocol   = in["kPrt"];
                
                if (memcmp(&cfg, &settings, sizeof(SettingsPack)) != 0) {
                    memcpy(&settings, &cfg, sizeof(SettingsPack));
                    settingsOk = true;
                    publish(Topic::STORAGE, &cfg, sizeof(SettingsPack));
                }
            }
            break;
        }
            
        case CMD_CORRECT_ODO: {
            int odo = inDoc["data"] | 0;
            if (odo == 0) odo = inDoc["data"]["value"] | 0;
            if (odo > 0) {
                CommandMsg c = {CMD_CORRECT_ODO, (float)odo};
                publish(Topic::SYSTEM, &c, sizeof(c));
            }
            break;
        }
            
        case CMD_START_TELEMETRY:
            telemetryActive = true;
            telemetryCounter = 0;
            lastTelemetryMs = 0;
            firstTelemetry = true;
            break;
            
        case CMD_STOP_TELEMETRY:
            telemetryActive = false;
            break;
            
        // --- OTA ---
        case CMD_OTA_UPDATE: {
            // Android: {"command":"ota_update","size":123456,"msg_id":5}
            int fwSize = inDoc["size"] | 0;
            if (fwSize > 0) {
                LOG_INFO(name, "OTA update: size=%d", fwSize);
                telemetryActive = false;  // останавливаем телеметрию
                otaFirmwareSize = fwSize;
                
                // Отправляем команду OTA-модулю
                CommandMsg c = {CMD_OTA_UPDATE, (float)fwSize};
                publish(Topic::SYSTEM, &c, sizeof(c));
                
                // ota_init ответ будет после получения CMD_OTA_INIT
            }
            break;
        }
        
        case CMD_OTA_DATA: {
            // Android: {"command":"ota_data","msg_id":6,"data":{"pack":1,"bin":"...","crc16":12345}}
            JsonObject d = inDoc["data"];
            if (d) {
                OtaChunk chunk;
                chunk.pack = d["pack"] | 0;
                chunk.crc16 = d["crc16"] | 0;
                const char* b64 = d["bin"] | "";
                chunk.bin_len = strlen(b64);
                strncpy(chunk.b64, b64, OTA_CHUNK_B64_SIZE);
                chunk.b64[OTA_CHUNK_B64_SIZE] = '\0';
                
                if (chunk.pack > 0 && chunk.bin_len > 0) {
                    publish(Topic::OTA, &chunk, sizeof(OtaChunk));
                }
            }
            break;
        }
        
        case CMD_OTA_END: {
            LOG_INFO(name, "OTA end");
            CommandMsg c = {CMD_OTA_END, 0};
            publish(Topic::SYSTEM, &c, sizeof(c));
            break;
        }
            
        // Команды шины
        case CMD_RESET_TRIP_A:
        case CMD_RESET_TRIP_B:
        case CMD_RESET_AVG:
        case CMD_FULL_TANK:
        case CMD_KL_GET_DTC:
        case CMD_KL_CLEAR_DTC:
        case CMD_KL_RESET_ADAPT:
        case CMD_KL_PUMP_ATF:
        case CMD_KL_DETECT_PROTO: {
            CommandMsg c = {(uint8_t)cmd, 0};
            publish(Topic::SYSTEM, &c, sizeof(c));
            break;
        }
            
        default:
            break;
    }
    
    if (inMsgId > 0) {
        outDoc["ack_id"] = inMsgId;
    }

    sendJson();
}

// ---------- Обработка get_cfg / set_cfg ----------

/**
 * @brief Формирование ответа на get_cfg.
 * 
 * Заполняет outDoc полями из сохранённых настроек или значениями по умолчанию.
 */
void Protocol::buildCfgResponse() {
    JsonObject cfg = outDoc["cfg"].to<JsonObject>();
    cfg["tV"]    = settingsOk ? settings.tank_capacity : 60.0f;
    cfg["iPerf"] = settingsOk ? settings.injector_flow : 250.0f;
    cfg["iCnt"]  = settingsOk ? settings.injector_count : 4;
    cfg["sSig"]  = settingsOk ? settings.pulses_per_meter : 3.0f;
    cfg["kPrt"]  = settingsOk ? settings.kline_protocol : 0;
    cfg["fw"]    = FW_VERSION_STR;
}

/**
 * @brief Обработка команды set_cfg.
 * 
 * Обновляет локальные настройки из поля "data" входящего JSON.
 * Устанавливает флаг settingsChanged при изменении.
 */
void Protocol::handleSetCfg() {
    JsonObject in = inDoc["data"];
    if (!in) return;
    
    // Создание копии текущих настроек
    SettingsPack newCfg = settings;
    
    // Обновление полей, если они присутствуют
    if (in["tV"])    newCfg.tank_capacity    = in["tV"];
    if (in["iPerf"]) newCfg.injector_flow    = in["iPerf"];
    if (in["iCnt"])  newCfg.injector_count   = in["iCnt"];
    if (in["sSig"])  newCfg.pulses_per_meter = in["sSig"];
    if (in["kPrt"])  newCfg.kline_protocol   = in["kPrt"];
    
    // Проверка изменений
    if (memcmp(&newCfg, &settings, sizeof(SettingsPack)) != 0) {
        memcpy(&settings, &newCfg, sizeof(SettingsPack));
        settingsOk = true;
        settingsChanged = true;
    }
}

// ---------- Реализация телеметрии ----------

/**
 * @brief Отправка пакета телеметрии.
 * 
 * Формирует и отправляет пакет, если разрешено и транспорт онлайн.
 * Ограничивает частоту отправки до 150 мс.
 */
void Protocol::sendTelemetry() {
    // Проверка условий отправки
    if (!telemetryActive || !transportOnline) return;
    
    unsigned long now = millis();
    
    // Ограничение частоты: минимум 150 мс между пакетами
    if (!firstTelemetry && now - lastTelemetryMs < 150) return;
    lastTelemetryMs = now;
    
    // Формирование базового пакета (каждые 150 мс)
    buildFastJson();
    
    // Добавление полей поездки каждые 4-й пакет (~600 мс)
    if (firstTelemetry || telemetryCounter % 4 == 0) {
        addTripFields();
    }
    
    // Добавление сервисных полей каждые 10-й пакет (~1.5 с)
    if (firstTelemetry || telemetryCounter % 10 == 0) {
        addServiceFields();
    }
    
    // Сброс флага первой отправки
    if (firstTelemetry) {
        firstTelemetry = false;
    } else {
        telemetryCounter++;
    }
    
    // Отправка сформированного JSON
    sendJson();
}

/**
 * @brief Формирование базового пакета телеметрии.
 * 
 * Заполняет поля, обновляемые каждые 150 мс:
 * - Скорость, обороты, напряжение
 * - Состояние двигателя, габаритов
 * - Положение селектора АКПП
 * - Уровень топлива
 */
void Protocol::buildFastJson() {
    outDoc.clear();
    JsonObject tel = outDoc["tel"].to<JsonObject>();
    
    tel["spd"]  = (int)(engineOk ? engine.speed : 0);
    tel["rpm"]  = (int)(engineOk ? engine.rpm : 0);
    tel["vlt"]  = roundf((engineOk ? engine.voltage : 0) * 10) / 10;
    tel["eng"]  = (int)(engineOk ? engine.engine_running : 0);
    tel["hl"]   = (int)(engineOk ? engine.parking_lights : 0);
    
    // Формирование строки положения селектора
    const char* selStr[] = {"P","R","N","D","3","2","L"};
    const char* base = (klineOk && kline.selector_position <= 6) ? selStr[kline.selector_position] : "D";
    char selBuf[8];
    if (klineOk && kline.current_gear > 0 && kline.selector_position == 3)
        snprintf(selBuf, sizeof(selBuf), "%s%d", base, kline.current_gear);
    else 
        snprintf(selBuf, sizeof(selBuf), "%s", base);
    tel["sel"] = selBuf;
    
    tel["tcc"] = (int)(klineOk ? kline.tcc_lockup : 0);
    tel["fuel"] = roundf((engineOk ? engine.fuel_level_sensor : 0) * 10) / 10;
}

/**
 * @brief Добавление полей поездки в телеметрию.
 * 
 * Добавляет поля, обновляемые каждые 600 мс:
 * - Общий пробег (odo)
 * - Поездки A/B и расход
 * - Текущий пробег и расход
 * - Мгновенный и средний расход
 * 
 * Округление до 1 знака после запятой.
 */
void Protocol::addTripFields() {
    if (!tripOk) return;
    
    JsonObject tel = outDoc["tel"];
    tel["odo"] = (int)trip.odo;
    tel["trip_a"]   = roundf(trip.trip_a * 10) / 10;
    tel["fuel_a"]   = roundf(trip.fuel_trip_a * 10) / 10;
    tel["trip_b"]   = roundf(trip.trip_b * 10) / 10;
    tel["fuel_b"]   = roundf(trip.fuel_trip_b * 10) / 10;
    tel["trip_cur"] = roundf(trip.trip_cur * 10) / 10;
    tel["fuel_cur"] = roundf(trip.fuel_cur * 10) / 10;
    tel["inst"]     = roundf((engineOk ? engine.instant_fuel : 0) * 10) / 10;
    tel["avg_cur"]  = roundf(trip.avg_consumption * 10) / 10;
    
    float avg = trip.avg_total > 0 ? trip.avg_total : trip.avg_consumption;
    tel["avg"] = roundf(avg * 10) / 10;
}

/**
 * @brief Добавление сервисных полей в телеметрию.
 * 
 * Добавляет поля, обновляемые каждые 1.5 с:
 * - Температура ОЖ и АКПП
 * - Коды ошибок (DTC)
 * - Температура в салоне и за бортом
 * - Давление в шинах, уровень омывайки
 * 
 * Округление до 1 знака после запятой.
 */
void Protocol::addServiceFields() {
    JsonObject tel = outDoc["tel"];
    
    if (klineOk) {
        tel["t_cool"] = roundf(kline.coolant_temp * 10) / 10;
        tel["t_atf"]  = roundf(kline.atf_temp * 10) / 10;
        tel["dtc"]    = kline.dtc_codes;
    }
    
    if (climateOk) {
        tel["t_int"] = roundf(climate.interior_temp * 10) / 10;
        tel["t_ext"] = roundf(climate.exterior_temp * 10) / 10;
        tel["tire"]  = (int)climate.tire_pressure;
        tel["wash"]  = (int)climate.washer_level;
    }
}

/**
 * @brief Отправка JSON-пакета.
 * 
 * Сериализует outDoc в строку и публикует в Topic::PROTOCOL.
 * 
 * @return true при успехе, false если документ пуст
 */
bool Protocol::sendJson() {
    size_t len = measureJson(outDoc);
    if (len <= 2) return false;
    
    char buf[512];
    len = serializeJson(outDoc, buf, sizeof(buf));
    publish(Topic::PROTOCOL, buf, len + 1);
    return true;
}