#ifndef CALCULATOR_H
#define CALCULATOR_H

#include "core/module.h"
#include "core/packets.h"

class Calculator : public Module {
public:
    Calculator() : Module("Calculator") {}
    
protected:
    bool onInit() override;
    void onProcess() override;
    void onCommand(const CommandMsg& cmd) override;
    void onData(uint16_t topic, const void* data) override;
    
private:
    // Base-значения от Storage
    double odoBase = 0;
    float  tripABase = 0, tripBBase = 0;
    float  fuelABase = 0, fuelBBase = 0;
    float  fuelBase = 60;
    bool   baseLoaded = false;
    bool   fuelLoaded = false;
    
    // Накопленные за поездку
    float  curDist = 0, curFuel = 0;
    
    // Средний расход
    float  avgCur = 0, avgTotal = 0;
    
    // Статус двигателя
    bool   engineRunning = false;
    bool   notFuel = true;
    
    // Настройки
    float  tankCap = 60;
    bool   settingsLoaded = false;
    
    // Периодика
    unsigned long lastPublish = 0;
};

#endif