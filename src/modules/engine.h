/**
 * @file engine.h
 * @brief Модуль двигателя — единый интерфейс для реального двигателя и симулятора.
 * 
 * Режим определяется по пину ENGINE_SENSE_PIN (GPIO25):
 * - HIGH (>2.0V = зажигание ON) → реальный двигатель (RMT + PCNT + INA226)
 * - LOW/GND → симулятор (кнопки, потенциометр, физика)
 * 
 * Делитель для ENGINE_SENSE_PIN: R1=47кОм (к +12V), R2=12кОм (к GND)
 * 16V → 3.25V, 11V → 2.24V
 * 
 * Аппаратные компоненты (работают всегда):
 * - INA226 (0x40) — точное напряжение бортсети
 * - ADC GPIO34 — датчик уровня топлива (поплавок Toyota через ОУ)
 * - RMT GPIO4 — форсунка (оптопара 6N137)
 * - PCNT GPIO13 — датчик скорости VSS (оптопара PC817)
 * 
 * Симулятор (когда зажигание OFF):
 * - Кнопка запуска двигателя (GPIO26, длинное нажатие 800мс)
 * - Кнопка габаритов (GPIO27, короткое нажатие)
 * - Потенциометр педали газа (GPIO33, ADC)
 * - Физика: инерция скорости, RPM по кривой, расход по формуле
 */

#ifndef ENGINE_H
#define ENGINE_H

#include "core/module.h"
#include "core/packets.h"
#include "ina226.h"

// Пины
#define ENGINE_SENSE_PIN      25    // Детектор зажигания ON (делитель 47к/12к)
#define ENGINE_INJECTOR_PIN   4     // Форсунка (оптопара 6N137 → RMT)
#define ENGINE_VSS_PIN       13     // Датчик скорости (оптопара PC817 → PCNT)
#define ENGINE_FUEL_PIN      34     // Датчик топлива (поплавок → ОУ → ADC)
#define ENGINE_BUTTON_START   26    // Кнопка запуска (симулятор)
#define ENGINE_BUTTON_LIGHTS  27    // Кнопка габаритов (симулятор)
#define ENGINE_POT_GAS       33     // Потенциометр газа (симулятор)

// Параметры форсунки (по умолчанию, переопределяются из настроек)
#define ENGINE_DEAD_TIME_US    750   // Мёртвая зона (мкс)
#define ENGINE_INJECTOR_FLOW   250   // Производительность (мл/мин)
#define ENGINE_INJECTOR_COUNT  4     // Количество форсунок
#define ENGINE_PULSES_PER_M    3.0f  // Импульсов VSS на метр
#define ENGINE_TANK_CAPACITY   60.0f // Объём бака (л)

// Симулятор
#define SIM_SPEED_MAX          220.0f
#define SIM_RPM_IDLE           750.0f
#define SIM_RPM_MAX            6500.0f

extern "C" {
    #include "driver/rmt.h"
    #include "driver/pcnt.h"
}

class Engine : public Module {
public:
    Engine() : Module("Engine") {}

protected:
    bool onInit() override;
    void onProcess() override;
    void onCommand(const CommandMsg& cmd) override;
    void onData(uint16_t topic, const void* data) override;

private:
    EnginePack _pack;
    unsigned long _lastPublish = 0;
    int _logCounter = 0;

    // === Режим ===
    bool _realMode = false;       ///< true = зажигание ON (GPIO25 HIGH)

    // === Настройки из Storage ===
    float _tankCapacity    = ENGINE_TANK_CAPACITY;
    float _injectorFlow    = ENGINE_INJECTOR_FLOW;
    uint8_t _injectorCount = ENGINE_INJECTOR_COUNT;
    float _pulsesPerMeter  = ENGINE_PULSES_PER_M;
    bool _fuelLoaded       = false;

    // === INA226 (всегда) ===
    INA226 _ina{0x40};
    bool _inaReady = false;
    float _voltage = 12.7f;

    // === Датчик топлива (ADC, всегда) ===
    float _fuelAdcSmooth = 0;
    bool _fuelAdcInit = false;
    float _fuelAdcEmpty = 1.59f;
    float _fuelAdcFull  = 3.30f;
    float readFuelLevel();

    // === Данные поездки ===
    float _distance = 0;
    float _fuelUsed = 0;
    float _fuelBase = ENGINE_TANK_CAPACITY;
    float _instantFuel = 0;        ///< Мгновенный расход (л/100км или л/ч)
    bool _engineRunning = false;
    bool _parkingLights = false;

    // === Реальный двигатель (RMT + PCNT) ===
    volatile uint64_t _injTotalUs = 0;
    volatile uint32_t _injPulses  = 0;
    float _lastVoltage = 13.8f;
    
    void initRMT();
    void initPCNT();
    void processRMTData();
    float readVssSpeed();
    float calcDeadTime(float voltage);
    
    // Калибровка
    bool _calSpeedActive = false;
    uint32_t _calSpeedPulses = 0;

    // === Симулятор ===
    bool _pedalConnected = false;
    float _speed = 0, _rpm = 0, _throttle = 0;
    float _filteredRaw = 0;
    unsigned long _lastPhysics = 0, _lastPotRead = 0;
    unsigned long _engineLastChange = 0, _enginePressStart = 0;
    unsigned long _lightsLastChange = 0;
    bool _engineLastState = HIGH, _lightsLastState = HIGH, _lightsHandled = false;
    
    float getRpm();
    float getInstantFuel();
    void processButtons(unsigned long now);
    void processPedal(unsigned long now);
    void processPhysics(unsigned long now);
    void publishPack(unsigned long now);
};

#endif