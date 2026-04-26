#include "calculator.h"
#include "core/message.h"
#include "core/logging.h"

bool Calculator::onInit() {
    subscribeNew(Topic::SENSOR,     sizeof(EnginePack));
    subscribeNew(Topic::CALCULATOR, sizeof(TripPack));
    subscribeNew(Topic::STORAGE,    sizeof(SettingsPack));
    return true;
}

void Calculator::onCommand(const CommandMsg& cmd) {
    switch (cmd.cmd) {
        case CMD_RESET_TRIP_A:
            tripABase = -curDist; fuelABase = -curFuel;
            LOG_INFO(name, "Trip A reset");
            break;
        case CMD_RESET_TRIP_B:
            tripBBase = -curDist; fuelBBase = -curFuel;
            LOG_INFO(name, "Trip B reset");
            break;
        case CMD_RESET_AVG:
            curDist = 0; curFuel = 0; avgCur = 0;
            LOG_INFO(name, "Avg reset");
            break;
        case CMD_FULL_TANK:
            fuelBase = tankCap; curFuel = 0;
            LOG_INFO(name, "Full tank: %.1f L", fuelBase);
            break;
        case CMD_CORRECT_ODO:
            odoBase = (double)cmd.value;
            LOG_INFO(name, "ODO corrected: %.0f", odoBase);
            break;
    }
}

void Calculator::onData(uint16_t topic, const void* data) {
    switch (topic) {
        case Topic::SENSOR: {
            const EnginePack* p = (const EnginePack*)data;
            
            if (p->engine_running && !engineRunning) {
                curDist = 0; curFuel = 0; avgCur = 0;
                LOG_INFO(name, "Engine started");
            }
            if (!p->engine_running && engineRunning) {
                avgTotal = (avgTotal == 0) ? avgCur : (avgTotal + avgCur) / 2;
                LOG_INFO(name, "Engine stopped — avg=%.1f", avgTotal);
                
                // Немедленная публикация для сохранения в Storage
                TripPack pack;
                memset(&pack, 0, sizeof(pack));
                pack.version         = 2;
                pack.odo             = odoBase + curDist;
                pack.trip_a          = tripABase + curDist;
                pack.fuel_trip_a     = fuelABase + curFuel;
                pack.trip_b          = tripBBase + curDist;
                pack.fuel_trip_b     = fuelBBase + curFuel;
                pack.trip_cur        = curDist;
                pack.fuel_cur        = curFuel;
                pack.fuel_level      = notFuel ? max(0.0f, fuelBase - curFuel) : 0;
                pack.avg_consumption = (curDist > 0.001f) ? (curFuel / curDist) * 100.0f : 0;
                pack.avg_total       = avgTotal;
                publish(Topic::CALCULATOR, &pack, sizeof(pack));
            }
            engineRunning = p->engine_running;
            curDist = p->distance;
            curFuel = p->fuel_used;
            notFuel = p->not_fuel;
            break;
        }
        
        case Topic::CALCULATOR: {
            const TripPack* p = (const TripPack*)data;
            if (!baseLoaded && p->odo > 0.01) {  // убрал !engineRunning
                odoBase   = p->odo;
                tripABase = p->trip_a; fuelABase = p->fuel_trip_a;
                tripBBase = p->trip_b; fuelBBase = p->fuel_trip_b;
                if (p->avg_total > 0) avgTotal = p->avg_total;
                baseLoaded = true;
                LOG_INFO(name, "Base: ODO=%.0f, tripA=%.1f, fuel=%.1f",
                         odoBase, tripABase, p->fuel_level);
            }
            if (!fuelLoaded && p->fuel_level > 0.01f) {
                fuelBase = p->fuel_level; fuelLoaded = true;
                LOG_INFO(name, "Fuel: %.1f L", fuelBase);
            }
            break;
        }
        
        case Topic::STORAGE: {
            const SettingsPack* p = (const SettingsPack*)data;
            if (!settingsLoaded) {
                settingsLoaded = true;
                LOG_INFO(name, "Settings: tank=%.1f", p->tank_capacity);
            }
            float old = tankCap;
            tankCap = p->tank_capacity;
            if (old != tankCap && fuelBase > tankCap) {
                fuelBase = tankCap;
            }
            break;
        }
    }
}

void Calculator::onProcess() {
    if (!baseLoaded) return;  // ждём загрузки из Storage
    
    unsigned long now = millis();
    if (now - lastPublish >= 1000) {
        lastPublish = now;
        
        TripPack pack;
        memset(&pack, 0, sizeof(pack));
        pack.version         = 2;
        pack.odo             = odoBase + curDist;
        pack.trip_a          = tripABase + curDist;
        pack.fuel_trip_a     = fuelABase + curFuel;
        pack.trip_b          = tripBBase + curDist;
        pack.fuel_trip_b     = fuelBBase + curFuel;
        pack.trip_cur        = curDist;
        pack.fuel_cur        = curFuel;
        pack.fuel_level      = notFuel ? max(0.0f, fuelBase - curFuel) : 0;
        pack.avg_consumption = (curDist > 0.001f) ? (curFuel / curDist) * 100.0f : 0;
        avgCur = pack.avg_consumption;
        pack.avg_total       = avgTotal;
        
        publish(Topic::CALCULATOR, &pack, sizeof(pack));
    }
}