#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "core/module.h"
#include "core/packets.h"

class Simulator : public Module {
public:
    Simulator() : Module("Simulator") {}
    
protected:
    bool onInit() override;
    void onProcess() override;
    void onCommand(const CommandMsg& cmd) override;
    void onData(uint16_t topic, const void* data) override;
    
private:
    EnginePack pack;
    bool engineRunning = false, parkingLights = false;
    float speed = 0, rpm = 0, throttle = 0;
    float distance = 0, fuelUsed = 0;
    float fuelBase = 60, tankCapacity = 60;
    bool fuelLoaded = false;
    unsigned long lastPhysics = 0, lastPublish = 0, lastPotRead = 0;
    float filteredRaw = 0;
    bool pedalConnected = false;
    
    static constexpr int PIN_ENGINE = 26, PIN_LIGHTS = 27, PIN_POT = 33;
    static volatile bool btnEngine, btnLights;
    static void IRAM_ATTR isrEngine();
    static void IRAM_ATTR isrLights();
    
    float getRpm();
    float getInstantFuel();
};

#endif