/**
 * @file packets.h
 * @brief Агрегированные пакеты данных для шины Router.
 * 
 * Взят из эталонной прошивки Bluetooth_connect_v2 v6.8.22.
 * #pragma pack(1) для прямой бинарной сериализации в NVS.
 */

#ifndef PACKETS_H
#define PACKETS_H

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// EnginePack — FAST-телеметрия (публикуется каждые 100 мс)
// =============================================================================
#pragma pack(push, 1)
typedef struct {
    uint8_t  version;           // Версия пакета (текущая: 3)
    float    speed;             // Скорость автомобиля (км/ч)
    float    rpm;               // Обороты двигателя (об/мин)
    float    voltage;           // Напряжение бортсети (В)
    bool     engine_running;    // Двигатель работает
    bool     parking_lights;    // Габаритные огни
    float    instant_fuel;      // Мгновенный расход (л/100км или л/ч)
    float    distance;          // Накопленный пробег за поездку (км)
    float    fuel_used;         // Накопленный расход за поездку (л)
    float    fuel_level_sensor; // Остаток топлива в баке (л)
    bool     not_fuel;          // true = датчик топлива отсутствует
} EnginePack;
#pragma pack(pop)

// =============================================================================
// TripPack — TRIP-телеметрия (публикуется каждые 1000 мс)
// =============================================================================
#pragma pack(push, 1)
typedef struct {
    uint8_t  version;           // Версия пакета (текущая: 2)
    double   odo;               // Общий пробег (км)
    float    trip_a;            // Пробег поездки A (км)
    float    fuel_trip_a;       // Расход топлива за поездку A (л)
    float    trip_b;            // Пробег поездки B (км)
    float    fuel_trip_b;       // Расход топлива за поездку B (л)
    float    trip_cur;          // Текущий пробег поездки (км)
    float    fuel_cur;          // Текущий расход поездки (л)
    float    fuel_level;        // Остаток топлива в баке (л)
    float    avg_consumption;   // Средний расход за текущую поездку (л/100км)
    float    avg_total;         // Накопленный средний расход за всё время (л/100км)
} TripPack;
#pragma pack(pop)

// =============================================================================
// KlinePack — K-LINE-телеметрия (публикуется каждые 1000 мс)
// =============================================================================
#pragma pack(push, 1)
typedef struct {
    uint8_t  version;           // Версия пакета (текущая: 2)
    float    coolant_temp;      // Температура ОЖ (°C)
    float    atf_temp;          // Температура масла АКПП (°C)
    bool     tcc_lockup;        // Блокировка гидротрансформатора
    uint8_t  selector_position; // Позиция селектора АКПП (0=P, 1=R, 2=N, 3=D...)
    uint8_t  current_gear;      // Текущая передача (1..6, 0=нейтраль)
    float    voltage;           // Напряжение бортсети от ЭБУ (В)
    float    fuel_percent;      // Уровень топлива (%)
    float    output_shaft_rpm;  // Обороты выходного вала АКПП
    uint8_t  dtc_count;         // Количество кодов ошибок
    char     dtc_codes[64];     // Коды ошибок через ';'
} KlinePack;
#pragma pack(pop)

// =============================================================================
// ClimatePack — КЛИМАТ-телеметрия (публикуется каждые 1000 мс)
// =============================================================================
#pragma pack(push, 1)
typedef struct {
    uint8_t  version;           // Версия пакета (текущая: 1)
    float    interior_temp;     // Температура в салоне (°C)
    float    exterior_temp;     // Температура за бортом (°C)
    bool     tire_pressure;     // Низкое давление в шинах
    bool     washer_level;      // Низкий уровень омывайки
} ClimatePack;
#pragma pack(pop)

// =============================================================================
// SettingsPack — Настройки (публикуется при старте и при изменении)
// =============================================================================
#pragma pack(push, 1)
typedef struct {
    uint8_t  version;           // Версия пакета (текущая: 1)
    float    tank_capacity;     // Ёмкость бака (л, по умолчанию 60.0)
    float    fuel_level;        // Уровень топлива в баке (л, по умолчанию 60.0 или загруженный из TripPack)
    uint8_t  injector_count;    // Количество форсунок (по умолчанию 4)
    float    injector_flow;     // Производительность форсунки (мл/мин, по умолчанию 250.0)
    float    pulses_per_meter;  // Импульсов датчика скорости на 1 метр
    uint8_t  kline_protocol;    // Протокол K-Line (0=авто)
} SettingsPack;
#pragma pack(pop)

#endif // PACKETS_H