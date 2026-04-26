#include "simulator.h"
#include "core/message.h"
#include "core/logging.h"

volatile bool Simulator::btnEngine = false;
volatile bool Simulator::btnLights = false;

void IRAM_ATTR Simulator::isrEngine() { btnEngine = true; }
void IRAM_ATTR Simulator::isrLights() { btnLights = true; }

bool Simulator::onInit() {
    memset(&pack, 0, sizeof(pack));
    pack.version = 3; pack.voltage = 12.7f; pack.not_fuel = true;
    
    // Подписка на настройки от Storage
    subscribeNew(Topic::STORAGE, sizeof(SettingsPack));
    
    pinMode(PIN_ENGINE, INPUT_PULLUP);
    pinMode(PIN_LIGHTS, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ENGINE), isrEngine, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_LIGHTS), isrLights, FALLING);
    
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
    }
}

void Simulator::onData(uint16_t topic, const void* data) {
    if (topic == Topic::STORAGE) {
        const SettingsPack* p = (const SettingsPack*)data;
        if (!fuelLoaded) { 
            fuelBase = p->tank_capacity; 
            fuelLoaded = true;
            LOG_INFO(name, "Settings loaded: tank=%.1f", p->tank_capacity);
        }
        tankCapacity = p->tank_capacity;
    }
}

void Simulator::onProcess() {
    unsigned long now = millis();
    
    // Кнопка двигателя
    if (btnEngine && digitalRead(PIN_ENGINE) == LOW) {
        static unsigned long pressStart;
        if (pressStart == 0) pressStart = now;
        if (now - pressStart >= 800) {
            engineRunning = !engineRunning;
            if (engineRunning) { 
                distance = 0; fuelUsed = 0;
                LOG_INFO(name, "Engine STARTED");
            } else {
                LOG_INFO(name, "Engine STOPPED");
            }
            pressStart = 0;
        }
    }
    
    // Габариты
    if (btnLights) {
        btnLights = false;
        if (digitalRead(PIN_LIGHTS) == HIGH) parkingLights = !parkingLights;
    }
    
    // Потенциометр
    if (now - lastPotRead >= 20) {
        lastPotRead = now;
        int raw = analogRead(PIN_POT);
        filteredRaw = 0.2f * raw + 0.8f * filteredRaw;
        throttle = constrain(filteredRaw / 4095.0f, 0.0f, 1.0f);
    }
    
    // Физика
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
    
    // Публикация
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
        publish(Topic::SENSOR, &pack, sizeof(pack));
        
        static int lc = 0; lc++;
        if (lc % 100 == 0) LOG_DEBUG(name, "Pub #%d: spd=%.1f, fuel=%.1f", lc, speed, fuelBase - fuelUsed);
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