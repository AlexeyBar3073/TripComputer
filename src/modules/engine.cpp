/**
 * @file engine.cpp
 * @brief Реализация модуля двигателя.
 * 
 * Режим определяется пином GPIO25 (через делитель 47к/12к от +12V зажигания).
 * HIGH = реальный двигатель, LOW = симулятор.
 * 
 * INA226 и датчик топлива работают всегда.
 */

#include "engine.h"
#include "core/message.h"
#include "core/logging.h"
#include <math.h>

// ============================================================================
// Инициализация
// ============================================================================

bool Engine::onInit() {
    memset(&_pack, 0, sizeof(_pack));
    _pack.version = 3;
    _pack.not_fuel = true;
    
    // Определяем режим по пину зажигания
    pinMode(ENGINE_SENSE_PIN, INPUT);
    _realMode = (analogRead(ENGINE_SENSE_PIN) > 2000);  // >2.0V = зажигание ON
    
    // INA226 — пробуем всегда
    _inaReady = _ina.begin(Wire, 0.1f);
    LOG_INFO(name, "INA226 %s", _inaReady ? "detected" : "not found");
    
    // Датчик топлива (ADC)
    analogReadResolution(12);
    analogSetWidth(12);
    pinMode(ENGINE_FUEL_PIN, INPUT);
    
    subscribeNew(Topic::STORAGE, sizeof(SettingsPack));
    
    if (_realMode) {
        // Реальный двигатель
        initRMT();
        initPCNT();
        LOG_INFO(name, "Init: REAL (RMT+PCNT+INA)");
    } else {
        // Симулятор
        pinMode(ENGINE_BUTTON_START,  INPUT_PULLUP);
        pinMode(ENGINE_BUTTON_LIGHTS, INPUT_PULLUP);
        analogReadResolution(12);
        analogSetAttenuation(ADC_11db);
        
        int sum = 0, minV = 4095, maxV = 0;
        for (int i = 0; i < 20; i++) {
            int v = analogRead(ENGINE_POT_GAS);
            sum += v; if (v < minV) minV = v; if (v > maxV) maxV = v;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        _pedalConnected = (maxV - minV < 200 && sum / 20 < 4000);
        _throttle = _pedalConnected ? 0 : 0.3f;
        LOG_INFO(name, "Init: SIM (pedal=%s)", _pedalConnected ? "yes" : "no");
    }
    
    return true;
}

// ============================================================================
// Команды и данные
// ============================================================================

void Engine::onCommand(const CommandMsg& cmd) {
    switch (cmd.cmd) {
        case CMD_FULL_TANK:
            _fuelBase = _tankCapacity;
            _fuelUsed = 0;
            LOG_INFO(name, "Full tank: %.1f L", _fuelBase);
            break;
        case CMD_CALIBRATE_SPEED:
            if (_realMode) {
                _calSpeedPulses = 0;
                _calSpeedActive = true;
                LOG_INFO(name, "VSS calibration start");
            }
            break;
    }
}

void Engine::onData(uint16_t topic, const void* data) {
    if (topic == Topic::STORAGE) {
        const SettingsPack* p = (const SettingsPack*)data;
        if (!_fuelLoaded) {
            _fuelLoaded = true;
            _fuelBase = (p->fuel_level > 0.01f) ? p->fuel_level : p->tank_capacity;
            LOG_INFO(name, "Fuel base: %.1f", _fuelBase);
        }
        _tankCapacity  = p->tank_capacity;
        _injectorFlow  = p->injector_flow;
        _injectorCount = p->injector_count;
        _pulsesPerMeter = p->pulses_per_meter;
    }
}

// ============================================================================
// Основной цикл
// ============================================================================

void Engine::onProcess() {
    unsigned long now = millis();
    
    // Напряжение (INA226 или 12.7V по умолчанию)
    if (_inaReady) {
        _voltage = _ina.readBusVoltage() / 1000.0f;
    }
    _pack.voltage = _voltage;
    
    if (_realMode) {
        // === Реальный двигатель ===
        processRMTData();
        
        if (now - _lastPublish >= 100) {
            _lastPublish = now;
            
            float speed = readVssSpeed();
            float deadTime = calcDeadTime(_voltage);
            
            uint64_t totalUs = _injTotalUs;
            uint32_t pulses  = _injPulses;
            _injTotalUs = 0;
            _injPulses  = 0;
            
            float totalSec = totalUs / 1000000.0f;
            float fuelMl = totalSec * (_injectorFlow * _injectorCount / 60.0f);
            float fuelLiters = fuelMl / 1000.0f;
            _fuelUsed += fuelLiters;
            
            float rpm = (pulses > 0 && totalSec > 0) 
                ? ((float)pulses / totalSec / _injectorCount) * 60.0f * 2.0f 
                : 0;
            
            _instantFuel = (speed > 5.0f) 
                ? (fuelLiters / (speed * 0.1f / 3600.0f)) * 100.0f 
                : (fuelLiters / 0.1f) * 3600.0f;  // л/ч на холостых
            
            _pack.speed             = speed;
            _pack.rpm               = roundf(rpm / 10) * 10;
            _pack.engine_running    = (pulses > 0);
            _pack.parking_lights    = false;
            _pack.instant_fuel      = _instantFuel;
            _pack.distance          = _distance;
            _pack.fuel_used         = _fuelUsed;
            _pack.fuel_level_sensor = readFuelLevel();
            _pack.not_fuel          = false;
            
            publish(Topic::SENSOR, &_pack, sizeof(_pack));
        }
    } else {
        // === Симулятор ===
        processButtons(now);
        processPedal(now);
        processPhysics(now);
        
        if (now - _lastPublish >= 100) {
            _lastPublish = now;
            
            _pack.speed             = _speed;
            _pack.rpm               = roundf(_rpm / 10) * 10;
            _pack.engine_running    = _engineRunning;
            _pack.parking_lights    = _parkingLights;
            _pack.instant_fuel      = _instantFuel;
            _pack.distance          = _distance;
            _pack.fuel_used         = _fuelUsed;
            _pack.fuel_level_sensor = readFuelLevel();
            _pack.not_fuel          = true;
            
            _logCounter++;
            if (_logCounter % 100 == 0) {
                LOG_DEBUG(name, "#%d: spd=%.1f, fuel=%.1f", _logCounter, _speed, _pack.fuel_level_sensor);
            }
            
            publish(Topic::SENSOR, &_pack, sizeof(_pack));
        }
    }
}

// ============================================================================
// Датчик топлива (ADC) — общий
// ============================================================================

float Engine::readFuelLevel() {
    int raw = analogRead(ENGINE_FUEL_PIN);
    float volts = raw * (3.3f / 4095.0f);
    
    if (!_fuelAdcInit) {
        _fuelAdcSmooth = volts;
        _fuelAdcInit = true;
    } else {
        _fuelAdcSmooth = _fuelAdcSmooth * 0.9f + volts * 0.1f;
    }
    
    float range = _fuelAdcFull - _fuelAdcEmpty;
    if (range > 0.01f) {
        float pct = (_fuelAdcSmooth - _fuelAdcEmpty) / range;
        pct = constrain(pct, 0.0f, 1.0f);
        return pct * _tankCapacity;
    }
    return fmaxf(0.0f, _fuelBase - _fuelUsed);
}

// ============================================================================
// Реальный двигатель (RMT + PCNT)
// ============================================================================

void Engine::initRMT() {
    rmt_config_t cfg;
    cfg.channel    = RMT_CHANNEL_0;
    cfg.gpio_num   = (gpio_num_t)ENGINE_INJECTOR_PIN;
    cfg.clk_div    = 80;
    cfg.mem_block_num = 1;
    cfg.rmt_mode   = RMT_MODE_RX;
    cfg.rx_config.filter_en = true;
    cfg.rx_config.filter_ticks_thresh = 50;
    cfg.rx_config.idle_threshold = 50000;
    
    ESP_ERROR_CHECK(rmt_config(&cfg));
    ESP_ERROR_CHECK(rmt_driver_install(cfg.channel, 1024, 0));
    rmt_rx_start(cfg.channel, true);
}

void Engine::initPCNT() {
    pcnt_config_t cfg;
    cfg.pulse_gpio_num = (gpio_num_t)ENGINE_VSS_PIN;
    cfg.ctrl_gpio_num  = -1;
    cfg.unit    = PCNT_UNIT_0;
    cfg.channel = PCNT_CHANNEL_0;
    cfg.pos_mode = PCNT_COUNT_INC;
    cfg.neg_mode = PCNT_COUNT_DIS;
    cfg.lctrl_mode = PCNT_MODE_KEEP;
    cfg.hctrl_mode = PCNT_MODE_KEEP;
    cfg.counter_h_lim = 30000;
    cfg.counter_l_lim = -30000;
    
    ESP_ERROR_CHECK(pcnt_unit_config(&cfg));
    ESP_ERROR_CHECK(pcnt_set_filter_value(PCNT_UNIT_0, 100));
    ESP_ERROR_CHECK(pcnt_filter_enable(PCNT_UNIT_0));
    ESP_ERROR_CHECK(pcnt_counter_pause(PCNT_UNIT_0));
    ESP_ERROR_CHECK(pcnt_counter_clear(PCNT_UNIT_0));
    ESP_ERROR_CHECK(pcnt_counter_resume(PCNT_UNIT_0));
}

void Engine::processRMTData() {
    RingbufHandle_t rb = NULL;
    rmt_get_ringbuf_handle(RMT_CHANNEL_0, &rb);
    if (!rb) return;
    
    size_t itemSize = 0;
    rmt_item32_t* items = (rmt_item32_t*)xRingbufferReceive(rb, &itemSize, 0);
    if (!items) return;
    
    uint32_t numItems = itemSize / sizeof(rmt_item32_t);
    uint64_t localUs = 0;
    uint32_t localPulses = 0;
    uint32_t deadTime = calcDeadTime(_voltage);
    
    for (uint32_t i = 0; i < numItems; i++) {
        if (items[i].level0 == 0 && items[i].duration0 > deadTime && items[i].duration0 < 50000) {
            localUs += items[i].duration0 - deadTime;
            localPulses++;
        }
        if (items[i].level1 == 0 && items[i].duration1 > deadTime && items[i].duration1 < 50000) {
            localUs += items[i].duration1 - deadTime;
            localPulses++;
        }
    }
    
    vRingbufferReturnItem(rb, (void*)items);
    
    portDISABLE_INTERRUPTS();
    _injTotalUs += localUs;
    _injPulses  += localPulses;
    portENABLE_INTERRUPTS();
}

float Engine::readVssSpeed() {
    int16_t cnt;
    pcnt_get_counter_value(PCNT_UNIT_0, &cnt);
    pcnt_counter_clear(PCNT_UNIT_0);
    
    float distM = (float)cnt / _pulsesPerMeter;
    _distance += distM / 1000.0f;
    return (distM / 0.1f) * 3.6f;
}

float Engine::calcDeadTime(float voltage) {
    return ENGINE_DEAD_TIME_US + (14.0f - voltage) * 120.0f;
}

// ============================================================================
// Симулятор
// ============================================================================

void Engine::processButtons(unsigned long now) {
    bool engineRaw = digitalRead(ENGINE_BUTTON_START);
    if (engineRaw != _engineLastState) {
        _engineLastChange = now;
        _engineLastState = engineRaw;
    }
    if (engineRaw == LOW && (now - _engineLastChange >= 50)) {
        if (_enginePressStart == 0) _enginePressStart = now;
        if (now - _enginePressStart >= 800) {
            _engineRunning = !_engineRunning;
            if (_engineRunning) { _distance = 0; _fuelUsed = 0; }
            LOG_INFO(name, "Engine %s", _engineRunning ? "STARTED" : "STOPPED");
            _enginePressStart = 0;
        }
    } else if (engineRaw == HIGH) {
        _enginePressStart = 0;
    }
    
    bool lightsRaw = digitalRead(ENGINE_BUTTON_LIGHTS);
    if (lightsRaw != _lightsLastState) {
        _lightsLastChange = now;
        _lightsLastState = lightsRaw;
    }
    if (lightsRaw == LOW && (now - _lightsLastChange >= 50)) {
        if (!_lightsHandled) {
            _lightsHandled = true;
            _parkingLights = !_parkingLights;
        }
    } else if (lightsRaw == HIGH) {
        _lightsHandled = false;
    }
}

void Engine::processPedal(unsigned long now) {
    if (now - _lastPotRead >= 20) {
        _lastPotRead = now;
        int raw = analogRead(ENGINE_POT_GAS);
        _filteredRaw = 0.2f * raw + 0.8f * _filteredRaw;
        _throttle = constrain(_filteredRaw / 4095.0f, 0.0f, 1.0f);
    }
}

void Engine::processPhysics(unsigned long now) {
    if (now - _lastPhysics >= 20) {
        _lastPhysics = now;
        if (_engineRunning) {
            float target = _throttle * SIM_SPEED_MAX;
            float step = 0.44f;
            _speed += (target > _speed ? step : -step);
            _speed = constrain(_speed, 0.0f, SIM_SPEED_MAX);
            _rpm = getRpm();
            
            float dt = 0.02f / 3600.0f;
            _distance += _speed * dt;
            _instantFuel = getInstantFuel();
            _fuelUsed += (_instantFuel * _speed * dt) / 100.0f;
        } else {
            _speed = fmaxf(0.0f, _speed - 0.44f);
            _rpm = 0;
            _instantFuel = 0;
        }
    }
}

float Engine::getRpm() {
    if (!_engineRunning) return 0;
    if (_speed <= 0.1f) return SIM_RPM_IDLE;
    if (_speed <= 60) return SIM_RPM_IDLE + _speed * 12.5f;
    return 1500 + (_speed - 60) * 25;
}

float Engine::getInstantFuel() {
    return _speed > 5 ? 5 + _speed / 10 : 0;
}