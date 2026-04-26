#include "storage.h"
#include "core/message.h"
#include "core/logging.h"
#include <cstring>

bool Storage::onInit() {
    subscribeNew(Topic::CALCULATOR, sizeof(TripPack));
    loadAndPublish();
    return true;
}

void Storage::onCommand(const CommandMsg& cmd) {
    if (cmd.cmd == CMD_GET_CFG) handleGetCfg((int)cmd.value);
}

void Storage::onData(uint16_t topic, const void* data) {
    if (topic == Topic::CALCULATOR) {
        const TripPack* p = (const TripPack*)data;
        LOG_DEBUG(name, "Trip: ODO=%.0f", p->odo);
        if (memcmp(&savedTrip, p, sizeof(TripPack)) != 0) {
            memcpy(&savedTrip, p, sizeof(TripPack));
            saveTrip();
            LOG_INFO(name, "Trip saved: ODO=%.0f", p->odo);
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
    
    TripPack trip; memset(&trip, 0, sizeof(trip));
    if (prefs.getBytes("trip", &trip, sizeof(trip)) != sizeof(TripPack)) {
        trip.version = 2; trip.fuel_level = 60;
    }
    memcpy(&savedTrip, &trip, sizeof(trip));
    LOG_INFO(name, "Loaded: ODO=%.0f, fuel=%.1f", trip.odo, trip.fuel_level);
    
    SettingsPack cfg; memset(&cfg, 0, sizeof(cfg));
    if (prefs.getBytes("settings", &cfg, sizeof(cfg)) != sizeof(SettingsPack)) {
        cfg.version = 1; cfg.tank_capacity = 60; cfg.injector_count = 4;
        cfg.injector_flow = 250; cfg.pulses_per_meter = 3;
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
    auto f = [](const char* j, const char* k, float d) {
        char s[32]; snprintf(s,32,"\"%s\":",k);
        const char* p = strstr(j, s); return p ? atof(p+strlen(s)) : d;
    };
    savedSettings.tank_capacity = f(json, "tV", savedSettings.tank_capacity);
    savedSettings.injector_flow = f(json, "iPerf", savedSettings.injector_flow);
    savedSettings.injector_count = (uint8_t)f(json, "iCnt", savedSettings.injector_count);
    savedSettings.pulses_per_meter = f(json, "sSig", savedSettings.pulses_per_meter);
    savedSettings.kline_protocol = (uint8_t)f(json, "kPrt", savedSettings.kline_protocol);
    settingsDirty = true;
    saveSettings(); settingsDirty = false;
    publish(Topic::STORAGE, &savedSettings, sizeof(SettingsPack));
}