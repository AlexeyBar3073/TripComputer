#include "simulator.h"
#include "core/message.h"
#include "core/logging.h"

bool Simulator::onInit() {
    memset(&pack, 0, sizeof(pack));
    pack.version = 3; pack.voltage = 12.7f; pack.not_fuel = true;
    
    subscribeNew(Topic::STORAGE, sizeof(SettingsPack));
    
    pinMode(PIN_ENGINE, INPUT_PULLUP);
    pinMode(PIN_LIGHTS, INPUT_PULLUP);
    
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    
    int sum = 0, minV = 4095, maxV = 0;
    for (int i = 0; i < 20; i++) {
        int v = analogRead(PIN_POT);
        sum += v; if (v < minV) minV = v; if (v > maxV) maxV = v;
        delay(2);
    }
    pedalConnected = (maxV - minV < 200 && sum / 20 < 4000);
    throttle = pedalConnected ? 0 : 0.3f;
    LOG_INFO(name, "Pedal %s", pedalConnected ? "connected" : "not connected");
    return true;
}

void Simulator::onCommand(const CommandMsg& cmd) {
    if (cmd.cmd == CMD_FULL_TANK) {
        fuelBase = tankCapacity; fuelUsed = 0;
        LOG_INFO(name, "Full tank: %.1f L", fuelBase);
    }
}

void Simulator::onData(uint16_t topic, const void* data) {
    if (topic == Topic::STORAGE) {
        const SettingsPack* p = (const SettingsPack*)data;
        if (!fuelLoaded) {
            fuelLoaded = true;
            // Если есть сохранённый остаток — берём его, иначе tank_capacity
            fuelBase = (p->fuel_level > 0.01f) ? p->fuel_level : p->tank_capacity;
            LOG_INFO(name, "Fuel base: %.1f (saved: %.1f, tank: %.1f)", 
                     fuelBase, p->fuel_level, p->tank_capacity);
        }
        tankCapacity = p->tank_capacity;
    }
}

void Simulator::onProcess() {
    unsigned long now = millis();
    
    // ====== Кнопка двигателя с антидребезгом ======
    static unsigned long engineLastChange = 0;
    static bool engineLastState = HIGH;
    static unsigned long enginePressStart = 0;
    bool engineRaw = digitalRead(PIN_ENGINE);
    
    if (engineRaw != engineLastState) {
        engineLastChange = now;
        engineLastState = engineRaw;
    }
    
    if (engineRaw == LOW && (now - engineLastChange >= 50)) {
        if (enginePressStart == 0) enginePressStart = now;
        if (now - enginePressStart >= 800) {
            engineRunning = !engineRunning;
            if (engineRunning) { 
                distance = 0; fuelUsed = 0;
                LOG_INFO(name, "Engine STARTED");
            } else {
                LOG_INFO(name, "Engine STOPPED");
            }
            enginePressStart = 0;
        }
    } else if (engineRaw == HIGH) {
        enginePressStart = 0;
    }
    
    // ====== Кнопка габаритов с антидребезгом ======
    static unsigned long lightsLastChange = 0;
    static bool lightsLastState = HIGH;
    static bool lightsHandled = false;
    bool lightsRaw = digitalRead(PIN_LIGHTS);
    
    if (lightsRaw != lightsLastState) {
        lightsLastChange = now;
        lightsLastState = lightsRaw;
    }
    
    if (lightsRaw == LOW && (now - lightsLastChange >= 50)) {
        if (!lightsHandled) {
            lightsHandled = true;
            parkingLights = !parkingLights;
            LOG_INFO(name, "Parking lights: %s", parkingLights ? "ON" : "OFF");
        }
    } else if (lightsRaw == HIGH) {
        lightsHandled = false;
    }
    
    // ====== Опрос потенциометра (каждые 20 мс) ======
    if (now - lastPotRead >= 20) {
        lastPotRead = now;
        int raw = analogRead(PIN_POT);
        filteredRaw = 0.2f * raw + 0.8f * filteredRaw;
        throttle = constrain(filteredRaw / 4095.0f, 0.0f, 1.0f);
    }
    
    // ====== Физика (каждые 20 мс) ======
    if (now - lastPhysics >= 20) {
        lastPhysics = now;
        if (engineRunning) {
            float target = throttle * 220.0f;
            float step = 0.44f;
            speed += (target > speed ? step : -step);
            speed = constrain(speed, 0.0f, 220.0f);
            rpm = getRpm();
            float dt = 0.02f / 3600.0f;
            distance += speed * dt;
            fuelUsed += (getInstantFuel() * speed * dt) / 100.0f;
        } else {
            speed = max(0.0f, speed - 0.44f);
            rpm = 0;
        }
    }
    
    // ====== Публикация EnginePack (каждые 100 мс) ======
    if (now - lastPublish >= 100) {
        lastPublish = now;
        pack.speed = speed;
        pack.rpm = roundf(rpm / 10) * 10;
        pack.voltage = engineRunning ? 13.5f + random(0,100)/100.0f : 12.7f;
        pack.engine_running = engineRunning;
        pack.parking_lights = parkingLights;
        pack.instant_fuel = getInstantFuel();
        pack.distance = distance;
        pack.fuel_used = fuelUsed;
        pack.fuel_level_sensor = max(0.0f, fuelBase - fuelUsed);
        
        static int lc = 0; lc++;
        if (lc % 500 == 0) {
            LOG_DEBUG(name, "Pub #%d: spd=%.1f, fuel=%.1f", lc, speed, pack.fuel_level_sensor);
        }
        
        publish(Topic::SENSOR, &pack, sizeof(pack));
    }
}

float Simulator::getRpm() {
    if (!engineRunning) return 0;
    if (speed <= 0.1f) return 750;
    if (speed <= 60) return 750 + speed * 12.5f;
    return 1500 + (speed - 60) * 25;
}

float Simulator::getInstantFuel() {
    return speed > 5 ? 5 + speed / 10 : 0;
}