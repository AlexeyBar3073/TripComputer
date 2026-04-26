#include "protocol.h"
#include "core/message.h"
#include "core/logging.h"

// ---------- Таблица ВСЕХ команд ----------
struct CmdEntry {
    const char* name;
    uint8_t     code;
};

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
};

static uint8_t findCommand(const char* name) {
    for (const auto& e : CMD_TABLE) {
        if (strcmp(name, e.name) == 0) return e.code;
    }
    return CMD_NONE;
}

// ---------- Protocol ----------

bool Protocol::onInit() {
    subscribeNew(Topic::TRANSPORT, 512);
    subscribeNew(Topic::SENSOR,     sizeof(EnginePack));
    subscribeNew(Topic::CALCULATOR, sizeof(TripPack));
    subscribeNew(Topic::KLINE,      sizeof(KlinePack));
    subscribeNew(Topic::SERVICE,    sizeof(ClimatePack));
    subscribeNew(Topic::STORAGE,    sizeof(SettingsPack));
    return true;
}

void Protocol::onCommand(const CommandMsg& cmd) {
    if (cmd.cmd == CMD_TRANSPORT_STATUS) {
        transportOnline = (cmd.value != 0);
        LOG_INFO(name, "Transport %s", transportOnline ? "ONLINE" : "OFFLINE");
    }
}

void Protocol::onData(uint16_t topic, const void* data) {
    switch (topic) {
        case Topic::TRANSPORT:  processIncoming((const char*)data); break;
        case Topic::SENSOR:     memcpy(&engine,   data, sizeof(EnginePack));   engineOk   = true; break;
        case Topic::CALCULATOR: memcpy(&trip,     data, sizeof(TripPack));     tripOk     = true; break;
        case Topic::KLINE:      memcpy(&kline,    data, sizeof(KlinePack));    klineOk    = true; break;
        case Topic::SERVICE:    memcpy(&climate,  data, sizeof(ClimatePack));  climateOk  = true; break;
        case Topic::STORAGE:
            memcpy(&settings, data, sizeof(SettingsPack));
            settingsOk = true;
            LOG_INFO(name, "Settings: tank=%.1f", settings.tank_capacity);
            break;
    }
}

void Protocol::onProcess() {
    sendTelemetry();
}

// ---------- Входящие команды ----------

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
            
        case CMD_SET_CFG:
            handleSetCfg();
            if (settingsChanged) {
                publish(Topic::STORAGE, json, strlen(json) + 1);
                settingsChanged = false;
            }
            break;
            
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
            
        // Команды, транслируемые в шину
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

// ---------- get_cfg / set_cfg ----------

void Protocol::buildCfgResponse() {
    JsonObject cfg = outDoc["cfg"].to<JsonObject>();
    cfg["tV"]    = settingsOk ? settings.tank_capacity : 60.0f;
    cfg["iPerf"] = settingsOk ? settings.injector_flow : 250.0f;
    cfg["iCnt"]  = settingsOk ? settings.injector_count : 4;
    cfg["sSig"]  = settingsOk ? settings.pulses_per_meter : 3.0f;
    cfg["kPrt"]  = settingsOk ? settings.kline_protocol : 0;
    cfg["fw"]    = "1.0";
}

void Protocol::handleSetCfg() {
    JsonObject in = inDoc["data"];
    if (!in) return;
    
    SettingsPack newCfg = settings;
    if (in["tV"])    newCfg.tank_capacity    = in["tV"];
    if (in["iPerf"]) newCfg.injector_flow    = in["iPerf"];
    if (in["iCnt"])  newCfg.injector_count   = in["iCnt"];
    if (in["sSig"])  newCfg.pulses_per_meter = in["sSig"];
    if (in["kPrt"])  newCfg.kline_protocol   = in["kPrt"];
    
    if (memcmp(&newCfg, &settings, sizeof(SettingsPack)) != 0) {
        memcpy(&settings, &newCfg, sizeof(SettingsPack));
        settingsOk = true;
        settingsChanged = true;
    }
}

// ---------- Телеметрия ----------

void Protocol::sendTelemetry() {
    if (!telemetryActive || !transportOnline) return;
    
    unsigned long now = millis();
    if (!firstTelemetry && now - lastTelemetryMs < 150) return;
    lastTelemetryMs = now;
    
    buildFastJson();
    
    if (firstTelemetry || telemetryCounter % 4 == 0) {
        addTripFields();
    }
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

void Protocol::buildFastJson() {
    outDoc.clear();
    JsonObject tel = outDoc["tel"].to<JsonObject>();
    tel["spd"]  = (int)(engineOk ? engine.speed : 0);
    tel["rpm"]  = (int)(engineOk ? engine.rpm : 0);
    tel["vlt"]  = roundf((engineOk ? engine.voltage : 0) * 10) / 10;
    tel["eng"]  = (int)(engineOk ? engine.engine_running : 0);
    tel["hl"]   = (int)(engineOk ? engine.parking_lights : 0);
    
    const char* selStr[] = {"P","R","N","D","3","2","L"};
    const char* base = (klineOk && kline.selector_position <= 6) ? selStr[kline.selector_position] : "D";
    char selBuf[8];
    if (klineOk && kline.current_gear > 0 && kline.selector_position == 3)
        snprintf(selBuf, sizeof(selBuf), "%s%d", base, kline.current_gear);
    else snprintf(selBuf, sizeof(selBuf), "%s", base);
    tel["sel"] = selBuf;
    tel["tcc"] = (int)(klineOk ? kline.tcc_lockup : 0);
    tel["fuel"] = roundf((engineOk ? engine.fuel_level_sensor : 0) * 10) / 10;
}

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

bool Protocol::sendJson() {
    size_t len = measureJson(outDoc);
    if (len <= 2) return false;
    
    char buf[512];
    len = serializeJson(outDoc, buf, sizeof(buf));
    publish(Topic::PROTOCOL, buf, len + 1);
    return true;
}