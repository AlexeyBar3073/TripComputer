#include "storage.h"
#include "core/message.h"
#include "core/logging.h"
#include <cstring>
#include <ArduinoJson.h>

bool Storage::onInit() {
    subscribeNew(Topic::CALCULATOR, sizeof(TripPack));
    subscribeNew(Topic::STORAGE, sizeof(SettingsPack));
    loadAndPublish();
    return true;
}

void Storage::onCommand(const CommandMsg& cmd) {
    if (cmd.cmd == CMD_GET_CFG) handleGetCfg((int)cmd.value);
}

void Storage::onData(uint16_t topic, const void* data) {

    if (topic == Topic::STORAGE) {
        const SettingsPack* p = (const SettingsPack*)data;
        if (memcmp(&savedSettings, p, sizeof(SettingsPack)) != 0) {
            memcpy(&savedSettings, p, sizeof(SettingsPack));
            saveSettings();
            LOG_INFO(name, "Settings saved: tank=%.1f", p->tank_capacity);
            // Публикуем обновлённые настройки для всех
            publish(Topic::STORAGE, p, sizeof(SettingsPack));
        }
    }

    if (topic == Topic::CALCULATOR) {
        const TripPack* p = (const TripPack*)data;
        
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
            //LOG_INFO(name, "Trip saved: ODO=%.0f, fuel=%.1f", p->odo, p->fuel_level);
        }
        
        if (fabsf(savedSettings.fuel_level - p->fuel_level) > 0.1f) {
            savedSettings.fuel_level = p->fuel_level;
            settingsDirty = true;
        }
    }
}

void Storage::onProcess() {
    unsigned long now = millis();
    if (settingsDirty && now - lastSettingsSave >= 2000) {
        saveSettings();
        lastSettingsSave = now;
        settingsDirty = false;
    }
}

void Storage::loadAndPublish() {
    prefs.begin("bkc_v2", false);
    
    TripPack trip;
    memset(&trip, 0, sizeof(trip));
    if (prefs.getBytes("trip", &trip, sizeof(trip)) != sizeof(TripPack)) {
        trip.version = 2;
        trip.fuel_level = 60;
    }
    memcpy(&savedTrip, &trip, sizeof(trip));
    LOG_INFO(name, "Loaded: ODO=%.0f, fuel=%.1f", trip.odo, trip.fuel_level);
    
    SettingsPack cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (prefs.getBytes("settings", &cfg, sizeof(cfg)) != sizeof(SettingsPack)) {
        cfg.version = 1;
        cfg.tank_capacity = 60;
        cfg.fuel_level = 0;
        cfg.injector_count = 4;
        cfg.injector_flow = 250;
        cfg.pulses_per_meter = 3;
    }
    memcpy(&savedSettings, &cfg, sizeof(cfg));
    
    prefs.end();
    
    publish(Topic::CALCULATOR, &trip, sizeof(trip));
    publish(Topic::STORAGE, &cfg, sizeof(cfg));
}

void Storage::saveTrip() {
    prefs.begin("bkc_v2", false);
    prefs.putBytes("trip", &savedTrip, sizeof(TripPack));
    prefs.end();
}

void Storage::saveSettings() {
    prefs.begin("bkc_v2", false);
    prefs.putBytes("settings", &savedSettings, sizeof(SettingsPack));
    prefs.end();
}

void Storage::handleGetCfg(int msgId) {
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\"msg_id\":%d,\"cfg\":{\"tV\":%.1f,\"iPerf\":%.1f,\"iCnt\":%d,\"sSig\":%.3f,\"kPrt\":%d,\"fw\":\"1.0\"}}",
        msgId, savedSettings.tank_capacity, savedSettings.injector_flow,
        savedSettings.injector_count, savedSettings.pulses_per_meter, savedSettings.kline_protocol);
    publish(Topic::PROTOCOL, buf, len + 1);
}

void Storage::handleSetCfg(const char* json) {
    // Парсим через ArduinoJson — он уже есть в проекте
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
    
    
}