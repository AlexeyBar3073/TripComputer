#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "core/module.h"
#include "core/packets.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <ArduinoJson.h>
#pragma GCC diagnostic pop

class Protocol : public Module {
public:
    Protocol() : Module("Protocol")
        , engineOk(false), tripOk(false), klineOk(false), climateOk(false), settingsOk(false)
        , settingsChanged(false)
        , telemetryActive(false), transportOnline(false)
        , lastTelemetryMs(0), telemetryCounter(0) {}
    
protected:
    bool onInit() override;
    void onProcess() override;
    void onCommand(const CommandMsg& cmd) override;
    void onData(uint16_t topic, const void* data) override;
    
private:
    EnginePack  engine;
    TripPack    trip;
    KlinePack   kline;
    ClimatePack climate;
    SettingsPack settings;
    bool engineOk, tripOk, klineOk, climateOk, settingsOk;
    bool settingsChanged;
    
    bool telemetryActive, transportOnline;
    unsigned long lastTelemetryMs;
    int telemetryCounter;
    bool firstTelemetry; 
    
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    StaticJsonDocument<512> inDoc;
    StaticJsonDocument<512> outDoc;
    #pragma GCC diagnostic pop
    
    void processIncoming(const char* json);
    void buildCfgResponse();
    void handleSetCfg();
    void sendTelemetry();
    void buildFastJson();
    void addTripFields();
    void addServiceFields();
    bool sendJson();
};

#endif