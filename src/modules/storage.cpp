/**
 * @file storage.cpp
 * @brief Хранитель настроек и данных между сеансами (NVS).
 * 
 * Storage загружает сохранённые TripPack и SettingsPack из Flash при старте,
 * публикует их для других модулей, и сохраняет при изменениях.
 * 
 * Настройки хранятся в формате JSON (ключ "settings") для устойчивости
 * к изменению структуры между версиями прошивки.
 * TripPack хранится бинарно (ключ "trip") — его структура стабильна.
 */

#include "storage.h"
#include "core/message.h"
#include "core/logging.h"
#include "core/version.h"
#include <cstring>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <ArduinoJson.h>
#pragma GCC diagnostic pop

bool Storage::onInit() {
    subscribeNew(Topic::CALCULATOR, sizeof(TripPack));  // для сохранения пробега
    subscribeNew(Topic::STORAGE, sizeof(SettingsPack)); // для получения новых настроек
    loadAndPublish();
    return true;
}

/**
 * @brief Обработка команд.
 * 
 * Storage больше не обрабатывает CMD_GET_CFG — это делает Protocol
 * (он кэширует настройки и отвечает мгновенно).
 * Storage только сохраняет данные.
 */
void Storage::onCommand(const CommandMsg& cmd) {
    // Нет команд для обработки
}

/**
 * @brief Обработка входящих данных.
 * 
 * При получении TripPack от Calculator — сравнивает с кэшем и сохраняет при изменениях.
 * При получении SettingsPack — обновляет кэш и помечает как «грязный» для сохранения.
 */
void Storage::onData(uint16_t topic, const void* data) {
    if (topic == Topic::CALCULATOR) {
        const TripPack* p = (const TripPack*)data;
        
        // Сравниваем только сохраняемые поля (не trip_cur, fuel_cur, avg_consumption)
        bool tripChanged = false;
        if (fabs((double)savedTrip.odo - (double)p->odo) > 0.1) tripChanged = true;
        if (fabsf(savedTrip.trip_a - p->trip_a) > 0.1) tripChanged = true;
        if (fabsf(savedTrip.fuel_trip_a - p->fuel_trip_a) > 0.1) tripChanged = true;
        if (fabsf(savedTrip.trip_b - p->trip_b) > 0.1) tripChanged = true;
        if (fabsf(savedTrip.fuel_trip_b - p->fuel_trip_b) > 0.1) tripChanged = true;
        if (fabsf(savedTrip.fuel_level - p->fuel_level) > 0.1) tripChanged = true;
        if (fabsf(savedTrip.avg_total - p->avg_total) > 0.1) tripChanged = true;
        
        if (tripChanged) {
            memcpy(&savedTrip, p, sizeof(TripPack));
            saveTrip();
            LOG_INFO(name, "Trip saved: ODO=%.0f, fuel=%.1f", p->odo, p->fuel_level);
        }
        
        // Обновляем fuel_level в SettingsPack при изменении
        if (fabsf(savedSettings.fuel_level - p->fuel_level) > 0.1f) {
            savedSettings.fuel_level = p->fuel_level;
            settingsDirty = true;
        }
    }
    else if (topic == Topic::STORAGE) {
        const SettingsPack* p = (const SettingsPack*)data;
        if (memcmp(&savedSettings, p, sizeof(SettingsPack)) != 0) {
            memcpy(&savedSettings, p, sizeof(SettingsPack));
            settingsDirty = true;
        }
    }
}

/**
 * @brief Периодическое сохранение настроек.
 * 
 * Сохраняет SettingsPack не чаще чем раз в 2 секунды после изменения.
 * TripPack сохраняется немедленно при изменении (в onData).
 */
void Storage::onProcess() {
    unsigned long now = millis();
    if (settingsDirty && now - lastSettingsSave >= 2000) {
        saveSettings();
        lastSettingsSave = now;
        settingsDirty = false;
    }
}

/**
 * @brief Загрузка данных из NVS и публикация для других модулей.
 * 
 * Вызывается один раз при старте.
 * TripPack загружается бинарно, SettingsPack — из JSON-строки.
 * При отсутствии данных создаются значения по умолчанию.
 */
void Storage::loadAndPublish() {
    prefs.begin("bkc_v2", false);
    
    // --- TripPack (бинарный) ---
    TripPack trip;
    memset(&trip, 0, sizeof(trip));
    size_t len = prefs.getBytes("trip", &trip, sizeof(trip));
    if (len != sizeof(TripPack) || trip.version < 1 || trip.version > 2) {
        trip.version = 2;
        trip.fuel_level = 60;
        LOG_INFO(name, "TripPack: defaults");
    } else {
        LOG_INFO(name, "Loaded: ODO=%.0f, fuel=%.1f", trip.odo, trip.fuel_level);
    }
    memcpy(&savedTrip, &trip, sizeof(trip));
    
    // --- SettingsPack (JSON для гибкости при смене версий) ---
    SettingsPack cfg;
    memset(&cfg, 0, sizeof(cfg));
    // Значения по умолчанию
    cfg.tank_capacity = 60;
    cfg.injector_count = 4;
    cfg.injector_flow = 250;
    cfg.pulses_per_meter = 3;
    
    String jsonStr = prefs.getString("settings", "");
    if (jsonStr.length() > 0) {
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        StaticJsonDocument<256> doc;
        #pragma GCC diagnostic pop
        DeserializationError err = deserializeJson(doc, jsonStr);
        if (!err) {
            // Читаем только те поля, которые есть в JSON (отсутствующие = по умолчанию)
            cfg.tank_capacity    = doc["tV"]    | cfg.tank_capacity;
            cfg.fuel_level       = doc["fuel"]  | cfg.fuel_level;
            cfg.injector_count   = doc["iCnt"]  | cfg.injector_count;
            cfg.injector_flow    = doc["iPerf"] | cfg.injector_flow;
            cfg.pulses_per_meter = doc["sSig"]  | cfg.pulses_per_meter;
            cfg.kline_protocol   = doc["kPrt"]  | cfg.kline_protocol;
            if (doc["fw"].is<const char*>()) {
                strncpy(cfg.fw_version, doc["fw"], sizeof(cfg.fw_version) - 1);
            }
            LOG_INFO(name, "Settings loaded: tank=%.1f", cfg.tank_capacity);
        } else {
            LOG_INFO(name, "Settings JSON parse error, using defaults");
        }
    } else {
        LOG_INFO(name, "Settings: defaults");
    }
    memcpy(&savedSettings, &cfg, sizeof(cfg));
    
    prefs.end();
    
    // Публикуем для всех модулей (retain)
    publish(Topic::CALCULATOR, &trip, sizeof(trip));
    publish(Topic::STORAGE, &cfg, sizeof(cfg));
}

void Storage::saveTrip() {
    prefs.begin("bkc_v2", false);
    prefs.putBytes("trip", &savedTrip, sizeof(TripPack));
    prefs.end();
}

void Storage::saveSettings() {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    StaticJsonDocument<256> doc;
    #pragma GCC diagnostic pop
    doc["tV"]    = savedSettings.tank_capacity;
    doc["fuel"]  = savedSettings.fuel_level;
    doc["iCnt"]  = savedSettings.injector_count;
    doc["iPerf"] = savedSettings.injector_flow;
    doc["sSig"]  = savedSettings.pulses_per_meter;
    doc["kPrt"]  = savedSettings.kline_protocol;
    doc["fw"]    = savedSettings.fw_version[0] ? savedSettings.fw_version : FW_VERSION_STR;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    
    prefs.begin("bkc_v2", false);
    prefs.putString("settings", jsonStr);
    prefs.end();
    
    LOG_INFO(name, "Settings saved: %s", jsonStr.c_str());
}

void Storage::handleGetCfg(int msgId) {
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\"msg_id\":%d,\"cfg\":{\"tV\":%.1f,\"iPerf\":%.1f,\"iCnt\":%d,\"sSig\":%.3f,\"kPrt\":%d,\"fw\":\"%s\"}}",
        msgId, savedSettings.tank_capacity, savedSettings.injector_flow,
        savedSettings.injector_count, savedSettings.pulses_per_meter, savedSettings.kline_protocol,
        savedSettings.fw_version[0] ? savedSettings.fw_version : FW_VERSION_STR);
    publish(Topic::PROTOCOL, buf, len + 1);
}

void Storage::handleSetCfg(const char* json) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    StaticJsonDocument<256> doc;
    #pragma GCC diagnostic pop
    DeserializationError err = deserializeJson(doc, json);
    if (err) return;
    
    JsonObject data = doc["data"];
    if (!data) return;
    
    if (data["tV"])    savedSettings.tank_capacity    = data["tV"];
    if (data["iPerf"]) savedSettings.injector_flow    = data["iPerf"];
    if (data["iCnt"])  savedSettings.injector_count   = data["iCnt"];
    if (data["sSig"])  savedSettings.pulses_per_meter = data["sSig"];
    if (data["kPrt"])  savedSettings.kline_protocol   = data["kPrt"];
    
    settingsDirty = true;
    saveSettings();
    settingsDirty = false;
    publish(Topic::STORAGE, &savedSettings, sizeof(SettingsPack));
    
    LOG_INFO(name, "Config updated: tank=%.1f", savedSettings.tank_capacity);
}