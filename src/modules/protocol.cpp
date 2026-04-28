/**
 * @file protocol.cpp
 * @brief Реализация JSON-протокола обмена данными.
 * 
 * Protocol — «бюрократ» системы. Принимает входящие JSON-команды от Transport,
 * парсит их, выполняет свою часть работы (get_cfg, set_cfg, телеметрия, OTA),
 * а остальные команды сквозняком передаёт в шину Topic::SYSTEM.
 * 
 * Формирует исходящие JSON-ответы и публикует их в Topic::PROTOCOL.
 * Телеметрия отправляется фракционно: FAST (150 мс), TRIP (600 мс), SERVICE (1.5 с).
 */

#include "protocol.h"
#include "core/message.h"
#include "core/logging.h"
#include "core/ota_chunk.h"
#include "core/version.h"

// ============================================================================
// Таблица всех команд: строковое имя → код команды
// ============================================================================

/**
 * @struct CmdEntry
 * @brief Связка «строка — код команды» для парсинга JSON.
 */
struct CmdEntry {
    const char* name;   ///< Имя команды в JSON (например, "get_cfg")
    uint8_t     code;   ///< Код команды (enum Command)
};

/**
 * @brief Таблица всех известных системе команд.
 * 
 * Содержит ВСЕ команды, включая те, которые Protocol не обрабатывает сам
 * (они передаются сквозняком в шину другим модулям).
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
    {"ota_update",          CMD_OTA_UPDATE},
    {"ota_data",            CMD_OTA_DATA},
    {"ota_end",             CMD_OTA_END},
    {"kl_get_dtc",          CMD_KL_GET_DTC},
    {"kl_clear_dtc",        CMD_KL_CLEAR_DTC},
    {"kl_reset_adapt",      CMD_KL_RESET_ADAPT},
    {"kl_pump_atf",         CMD_KL_PUMP_ATF},
    {"kl_detect_protocol",  CMD_KL_DETECT_PROTO},
};

/**
 * @brief Поиск кода команды по строковому имени.
 * @param name Имя команды (например, "get_cfg")
 * @return Код команды, или CMD_NONE если команда не найдена
 */
static uint8_t findCommand(const char* name) {
    for (const auto& e : CMD_TABLE) {
        if (strcmp(name, e.name) == 0) return e.code;
    }
    return CMD_NONE;
}

// ============================================================================
// Инициализация
// ============================================================================

bool Protocol::onInit() {
    // Подписка на все источники данных для формирования телеметрии
    subscribeNew(Topic::TRANSPORT,  512);                 // Входящие JSON-команды от клиента
    subscribeNew(Topic::SENSOR,     sizeof(EnginePack));  // Данные с датчиков двигателя
    subscribeNew(Topic::CALCULATOR, sizeof(TripPack));    // Расчётные данные (пробег, расход)
    subscribeNew(Topic::KLINE,      sizeof(KlinePack));   // Данные K-Line диагностики
    subscribeNew(Topic::SERVICE,    sizeof(ClimatePack)); // Климатические данные
    subscribeNew(Topic::STORAGE,    sizeof(SettingsPack));// Настройки устройства

    // При первом получении настроек проверим версию прошивки (OTA success)
    _needVersionCheck = true;
    return true;
}

// ============================================================================
// Обработка системных команд (вызывается из Module::process)
// ============================================================================

void Protocol::onCommand(const CommandMsg& cmd) {
    switch (cmd.cmd) {
        
        // --- Статус транспортного канала (BT подключился / отключился) ---
        case CMD_TRANSPORT_STATUS:
            transportOnline = (cmd.value != 0);
            LOG_INFO(name, "Transport %s", transportOnline ? "ONLINE" : "OFFLINE");
            
            // Если ожидается отправка ota_success — отправляем при подключении
            if (transportOnline && _pendingOtaSuccess) {
                _pendingOtaSuccess = false;
                outDoc.clear();
                outDoc["ota_success"] = true;
                outDoc["version"] = FW_VERSION_STR;
                sendJson();
            }
            break;
            
        // --- OTA: модуль готов к приёму чанков, отправляем ota_init клиенту ---
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
        
        // --- OTA: результат записи чанка (успех — номер, ошибка — запрос повтора) ---
        case CMD_OTA_WRITE: {
            int pack = (int)cmd.value;
            outDoc.clear();
            if (pack > 0) {
                outDoc["ota_read"] = pack;          // Подтверждение успешной записи
            } else {
                JsonObject replay = outDoc["ota_replay"].to<JsonObject>();
                replay["pack"] = -pack;              // Запрос повторной отправки чанка
            }
            sendJson();
            break;
        }
        
        // --- OTA: перезагрузка после успешной прошивки ---
        case CMD_OTA_RESTART:
            outDoc.clear();
            outDoc["ota_restart"] = 1;
            sendJson();
            break;
    }
}

// ============================================================================
// Приём данных из подписанных топиков (вызывается из Module::process)
// ============================================================================

void Protocol::onData(uint16_t topic, const void* data) {
    switch (topic) {
        case Topic::TRANSPORT:
            // Входящая JSON-команда от Bluetooth-клиента
            processIncoming((const char*)data);
            break;
            
        case Topic::SENSOR:
            // FAST-данные от двигателя (скорость, обороты, напряжение, расход)
            memcpy(&engine, data, sizeof(EnginePack));
            engineOk = true;
            break;
            
        case Topic::CALCULATOR:
            // TRIP-данные от вычислителя (пробег, расход, средние)
            memcpy(&trip, data, sizeof(TripPack));
            tripOk = true;
            break;
            
        case Topic::KLINE:
            // Данные K-Line диагностики (температуры, АКПП, коды ошибок)
            memcpy(&kline, data, sizeof(KlinePack));
            klineOk = true;
            break;
            
        case Topic::SERVICE:
            // Климатические данные (температуры салона/улицы, давление шин)
            memcpy(&climate, data, sizeof(ClimatePack));
            climateOk = true;
            break;
            
        case Topic::STORAGE:
            // Настройки устройства (при старте или после set_cfg)
            memcpy(&settings, data, sizeof(SettingsPack));
            settingsOk = true;
            LOG_INFO(name, "Settings: tank=%.1f", settings.tank_capacity);
            
            // Проверка версии прошивки: если изменилась — OTA прошёл успешно
            if (_needVersionCheck) {
                _needVersionCheck = false;
                if (strcmp(settings.fw_version, FW_VERSION_STR) != 0 && settings.fw_version[0] != '\0') {
                    LOG_INFO(name, "OTA success: %s -> %s", settings.fw_version, FW_VERSION_STR);
                    _pendingOtaSuccess = true;
                    
                    // Если транспорт уже онлайн — отправляем ota_success сразу
                    if (transportOnline) {
                        _pendingOtaSuccess = false;
                        outDoc.clear();
                        outDoc["ota_success"] = true;
                        outDoc["version"] = FW_VERSION_STR;
                        sendJson();
                    }
                }
                // Обновляем сохранённую версию
                strncpy(settings.fw_version, FW_VERSION_STR, sizeof(settings.fw_version));
                publish(Topic::STORAGE, &settings, sizeof(SettingsPack));
            }
            break;
    }
}

// ============================================================================
// Фоновая работа (вызывается из Module::process)
// ============================================================================

void Protocol::onProcess() {
    // Отправка отложенного ota_success (если транспорт появился после проверки версии)
    if (_pendingOtaSuccess && transportOnline) {
        LOG_INFO(name, "Sending OTA success...");
        _pendingOtaSuccess = false;
        outDoc.clear();
        outDoc["ota_success"] = true;
        outDoc["version"] = FW_VERSION_STR;
        sendJson();
    }
    
    // Отправка телеметрии (если активна и транспорт онлайн)
    sendTelemetry();
}

// ============================================================================
// Диспетчер входящих JSON-команд
// ============================================================================

/**
 * @brief Главный диспетчер входящих команд.
 * 
 * Разбирает JSON, определяет команду, вызывает соответствующий обработчик.
 * Команды, которые Protocol не обрабатывает сам — передаёт сквозняком в шину
 * через Topic::SYSTEM (их получат Calculator, KLine, Engine и другие модули).
 * Всегда добавляет ack_id к ответу, если входящий msg_id > 0.
 * 
 * @param json Указатель на строку с JSON-командой
 */
void Protocol::processIncoming(const char* json) {
    // 1. Парсим входящий JSON
    inDoc.clear();
    DeserializationError err = deserializeJson(inDoc, json);
    
    const char* cmdStr = inDoc["command"] | inDoc["cmd"];
    int inMsgId = inDoc["msg_id"] | 0;
    
    // Готовим исходящий документ
    outDoc.clear();
    
    // 2. Если JSON битый или без команды — просто ACK
    if (err || !cmdStr) {
        if (inMsgId > 0) outDoc["ack_id"] = inMsgId;
        sendJson();
        return;
    }
    
    LOG_INFO(name, "Cmd: %s, id=%d", cmdStr, inMsgId);
    
    // 3. Определяем код команды по строковому имени
    Command cmd = (Command)findCommand(cmdStr);
    
    // 4. Выполняем команду
    switch (cmd) {
        
        // === Запрос настроек ===
        case CMD_GET_CFG: {
            // Формируем JSON с текущими настройками из кэша (или значениями по умолчанию)
            JsonObject cfg = outDoc["cfg"].to<JsonObject>();
            cfg["tV"]    = settingsOk ? settings.tank_capacity : 60.0f;
            cfg["iPerf"] = settingsOk ? settings.injector_flow : 250.0f;
            cfg["iCnt"]  = settingsOk ? settings.injector_count : 4;
            cfg["sSig"]  = settingsOk ? settings.pulses_per_meter : 3.0f;
            cfg["kPrt"]  = settingsOk ? settings.kline_protocol : 0;
            cfg["fw"]    = FW_VERSION_STR;
            break;
        }
        
        // === Установка настроек ===
        case CMD_SET_CFG:
            // Парсим поле "data", обновляем кэш, отправляем в Storage для сохранения
            cmdSetCfg(json);
            break;
            
        // === Корректировка одометра ===
        case CMD_CORRECT_ODO: {
            // Извлекаем значение: data как число или data.value как число
            int odo = inDoc["data"] | 0;
            if (odo == 0) odo = inDoc["data"]["value"] | 0;
            if (odo > 0) {
                CommandMsg c = {CMD_CORRECT_ODO, (float)odo};
                publish(Topic::SYSTEM, &c, sizeof(c));
            }
            break;
        }
            
        // === Запуск телеметрии ===
        case CMD_START_TELEMETRY:
            // Взводим флаг, сбрасываем счётчики для первой полной отправки
            telemetryActive = true;
            telemetryCounter = 0;
            lastTelemetryMs = 0;
            firstTelemetry = true;
            break;
            
        // === Остановка телеметрии ===
        case CMD_STOP_TELEMETRY:
            telemetryActive = false;
            break;
            
        // === OTA: начало обновления ===
        case CMD_OTA_UPDATE:
            // Останавливаем телеметрию, сохраняем размер прошивки, запускаем OTA-модуль
            cmdOtaUpdate();
            break;
            
        // === OTA: данные чанка ===
        case CMD_OTA_DATA:
            // Извлекаем pack, crc16, base64-строку и отправляем в OTA-модуль через Topic::OTA
            cmdOtaData();
            break;
            
        // === OTA: завершение ===
        case CMD_OTA_END: {
            LOG_INFO(name, "OTA end");
            CommandMsg c = {CMD_OTA_END, 0};
            publish(Topic::SYSTEM, &c, sizeof(c));
            break;
        }
        
        // === Все остальные команды — сквозняком в шину ===
        // Их получат модули-подписчики через свои _cmdQueue:
        // Calculator: CMD_RESET_TRIP_A/B, CMD_RESET_AVG, CMD_FULL_TANK
        // KLine: CMD_KL_GET_DTC, CMD_KL_CLEAR_DTC, CMD_KL_RESET_ADAPT, ...
        default:
            if (cmd != CMD_NONE) {
                CommandMsg c = {(uint8_t)cmd, 0};
                publish(Topic::SYSTEM, &c, sizeof(c));
            }
            break;
    }
    
    // 5. Всегда добавляем ack_id к ответу (квитанция доставки)
    if (inMsgId > 0) {
        outDoc["ack_id"] = inMsgId;
    }
    
    sendJson();
}

// ============================================================================
// Обработчики сложных команд
// ============================================================================

/**
 * @brief Обработка команды set_cfg.
 * 
 * Парсит поле "data" из входящего JSON, обновляет кэш настроек,
 * публикует новый SettingsPack в Topic::STORAGE для сохранения на флеш.
 * 
 * @param rawJson Исходная JSON-строка (не используется, оставлена для совместимости)
 */
void Protocol::cmdSetCfg(const char* rawJson) {
    JsonObject in = inDoc["data"];
    if (!in) return;
    
    // Создаём копию текущих настроек и обновляем поля, которые присутствуют в JSON
    SettingsPack cfg = settings;
    if (in["tV"])    cfg.tank_capacity    = in["tV"];
    if (in["iPerf"]) cfg.injector_flow    = in["iPerf"];
    if (in["iCnt"])  cfg.injector_count   = in["iCnt"];
    if (in["sSig"])  cfg.pulses_per_meter = in["sSig"];
    if (in["kPrt"])  cfg.kline_protocol   = in["kPrt"];
    
    // Отправляем в Storage только если есть реальные изменения
    if (memcmp(&cfg, &settings, sizeof(SettingsPack)) != 0) {
        memcpy(&settings, &cfg, sizeof(SettingsPack));
        settingsOk = true;
        publish(Topic::STORAGE, &cfg, sizeof(SettingsPack));
    }
}

/**
 * @brief Обработка команды ota_update.
 * 
 * Останавливает потоковую телеметрию (освобождает память и канал),
 * сохраняет размер прошивки, отправляет команду CMD_OTA_UPDATE в OTA-модуль.
 */
void Protocol::cmdOtaUpdate() {
    int fwSize = inDoc["size"] | 0;
    if (fwSize > 0) {
        LOG_INFO(name, "OTA update: size=%d", fwSize);
        telemetryActive = false;          // Останавливаем телеметрию
        otaFirmwareSize = fwSize;         // Сохраняем размер для расчёта количества чанков
        CommandMsg c = {CMD_OTA_UPDATE, (float)fwSize};
        publish(Topic::SYSTEM, &c, sizeof(c));
    }
}

/**
 * @brief Обработка команды ota_data.
 * 
 * Извлекает из JSON поля: pack (номер чанка), crc16 (контрольная сумма),
 * bin (base64-строка с данными). Формирует структуру OtaChunk и публикует
 * её в Topic::OTA для обработки OTA-модулем.
 */
void Protocol::cmdOtaData() {
    JsonObject d = inDoc["data"];
    if (!d) return;
    
    OtaChunk chunk;
    chunk.pack   = d["pack"] | 0;
    chunk.crc16  = d["crc16"] | 0;
    const char* b64 = d["bin"] | "";
    chunk.bin_len = strlen(b64);
    strncpy(chunk.b64, b64, OTA_CHUNK_B64_SIZE);
    chunk.b64[OTA_CHUNK_B64_SIZE] = '\0';
    
    if (chunk.pack > 0 && chunk.bin_len > 0) {
        publish(Topic::OTA, &chunk, sizeof(OtaChunk));
    }
}

// ============================================================================
// Телеметрия
// ============================================================================

/**
 * @brief Отправка пакета телеметрии.
 * 
 * Формирует и отправляет пакет, если телеметрия активна и транспорт онлайн.
 * Частота: каждые 150 мс.
 * Первая отправка — полная (FAST + TRIP + SERVICE).
 * Далее: FAST каждый пакет, TRIP каждый 4-й (~600 мс), SERVICE каждый 10-й (~1.5 с).
 */
void Protocol::sendTelemetry() {
    if (!telemetryActive || !transportOnline) return;
    
    unsigned long now = millis();
    if (!firstTelemetry && now - lastTelemetryMs < 150) return;
    lastTelemetryMs = now;
    
    // Формируем базовый пакет (скорость, обороты, напряжение, селектор, топливо)
    buildFastJson();
    
    // Каждые 4 пакета добавляем trip-поля (пробег, расход, средние)
    if (firstTelemetry || telemetryCounter % 4 == 0) {
        addTripFields();
    }
    
    // Каждые 10 пакетов добавляем service-поля (температуры, DTC, климат)
    if (firstTelemetry || telemetryCounter % 10 == 0) {
        addServiceFields();
    }
    
    if (firstTelemetry) {
        firstTelemetry = false;
    } else {
        telemetryCounter++;
    }
    
    sendJson();
}

/**
 * @brief Формирование FAST-пакета телеметрии (каждые 150 мс).
 * 
 * Поля: скорость, обороты, напряжение, статус двигателя, габариты,
 * положение селектора АКПП, блокировка ГТ, уровень топлива.
 */
void Protocol::buildFastJson() {
    outDoc.clear();
    JsonObject tel = outDoc["tel"].to<JsonObject>();
    
    tel["spd"]  = (int)(engineOk ? engine.speed : 0);
    tel["rpm"]  = (int)(engineOk ? engine.rpm : 0);
    tel["vlt"]  = roundf((engineOk ? engine.voltage : 0) * 10) / 10;
    tel["eng"]  = (int)(engineOk ? engine.engine_running : 0);
    tel["hl"]   = (int)(engineOk ? engine.parking_lights : 0);
    
    // Формируем строку положения селектора: "P", "R", "N", "D", "D2", "3", "2", "L"
    const char* selStr[] = {"P","R","N","D","3","2","L"};
    const char* base = (klineOk && kline.selector_position <= 6) ? selStr[kline.selector_position] : "D";
    char selBuf[8];
    if (klineOk && kline.current_gear > 0 && kline.selector_position == 3)
        snprintf(selBuf, sizeof(selBuf), "%s%d", base, kline.current_gear);  // "D2"
    else
        snprintf(selBuf, sizeof(selBuf), "%s", base);
    tel["sel"] = selBuf;
    
    tel["tcc"]  = (int)(klineOk ? kline.tcc_lockup : 0);
    tel["fuel"] = roundf((engineOk ? engine.fuel_level_sensor : 0) * 10) / 10;
}

/**
 * @brief Добавление TRIP-полей в телеметрию (каждые 600 мс).
 * 
 * Поля: общий пробег, поездки A/B, расход по поездкам, текущий пробег/расход,
 * мгновенный расход, средний расход.
 */
void Protocol::addTripFields() {
    if (!tripOk) return;
    
    JsonObject tel = outDoc["tel"];
    tel["odo"]      = (int)trip.odo;
    tel["trip_a"]   = roundf(trip.trip_a * 10) / 10;
    tel["fuel_a"]   = roundf(trip.fuel_trip_a * 10) / 10;
    tel["trip_b"]   = roundf(trip.trip_b * 10) / 10;
    tel["fuel_b"]   = roundf(trip.fuel_trip_b * 10) / 10;
    tel["trip_cur"] = roundf(trip.trip_cur * 10) / 10;
    tel["fuel_cur"] = roundf(trip.fuel_cur * 10) / 10;
    tel["inst"]     = roundf((engineOk ? engine.instant_fuel : 0) * 10) / 10;
    tel["avg_cur"]  = roundf(trip.avg_consumption * 10) / 10;
    
    // Если есть накопленный средний расход — показываем его, иначе текущий
    float avg = trip.avg_total > 0 ? trip.avg_total : trip.avg_consumption;
    tel["avg"] = roundf(avg * 10) / 10;
}

/**
 * @brief Добавление SERVICE-полей в телеметрию (каждые 1.5 с).
 * 
 * Поля: температура ОЖ и АКПП, коды ошибок, температура салона/улицы,
 * давление в шинах, уровень омывайки.
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

// ============================================================================
// Отправка JSON
// ============================================================================

/**
 * @brief Сериализация и отправка исходящего JSON.
 * 
 * Проверяет, что документ не пустой (больше 2 символов — не "{}"),
 * сериализует outDoc в buf и публикует в Topic::PROTOCOL.
 * 
 * @return true если данные отправлены, false если документ пуст
 */
bool Protocol::sendJson() {
    size_t len = measureJson(outDoc);
    if (len <= 2) return false;  // Пустой документ "{}" — не отправляем
    
    char buf[512];
    len = serializeJson(outDoc, buf, sizeof(buf));
    publish(Topic::PROTOCOL, buf, len + 1);
    return true;
}