/**
 * @file calculator.cpp
 * @brief Реализация модуля расчёта расхода топлива и пробега.
 * 
 * Выполняет расчёт среднего расхода, управляет поездками A/B,
 * корректирует одометр и обновляет данные каждую секунду.
 */

#include "calculator.h"
#include "core/message.h"
#include "core/logging.h"
#include <math.h>

/**
 * @brief Инициализация модуля Calculator.
 * 
 * Подписывается на три топика:
 * - Topic::SENSOR     — для получения данных с датчиков (скорость, обороты)
 * - Topic::CALCULATOR — для загрузки сохранённых значений при старте
 * - Topic::STORAGE    — для получения настроек (ёмкость бака)
 * 
 * @return true — всегда успешна
 */
bool Calculator::onInit() {
    // Создание новых очередей и подписка на топики.
    // Используется метод Module::subscribeNew, который создает очередь с глубиной 1 (OVERWRITE).
    // Размер элемента данных указан как sizeof(EnginePack) и т.д.
    subscribeNew(Topic::SENSOR,     sizeof(EnginePack));
    subscribeNew(Topic::CALCULATOR, sizeof(TripPack));
    subscribeNew(Topic::STORAGE,    sizeof(SettingsPack));
    return true;
}

/**
 * @brief Обработка входящих команд.
 * 
 * Реагирует на команды управления:
 * - Сброс поездки A/B
 * - Сброс среднего расхода
 * - Установка полного бака
 * - Корректировка одометра
 * 
 * @param cmd Структура команды (код + значение)
 */
void Calculator::onCommand(const CommandMsg& cmd) {
    // Обработка команды по её коду.
    switch (cmd.cmd) {
        
        // Команда: сброс поездки A.
        case CMD_RESET_TRIP_A:
            // Сброс выполняется путём смещения базовых значений.
            // База для расстояния и расхода устанавливается в -curDist и -curFuel.
            // При следующем расчете результата (base + cur) текущее значение будет равно нулю.
            tripABase = -curDist; 
            fuelABase = -curFuel;
            LOG_INFO(name, "Trip A reset");
            break;

        // Команда: сброс поездки B.
        case CMD_RESET_TRIP_B:
            tripBBase = -curDist; 
            fuelBBase = -curFuel;
            LOG_INFO(name, "Trip B reset");
            break;

        // Команда: сброс текущего среднего расхода.
        case CMD_RESET_AVG:
            // Сброс текущих данных поездки для нового цикла расчёта.
            curDist = 0; 
            curFuel = 0; 
            avgCur = 0;
            LOG_INFO(name, "Avg reset");
            break;

        // Команда: установка полного бака.
        case CMD_FULL_TANK:
            // Установка базового уровня топлива равным ёмкости бака.
            // Текущий расход сбрасывается, чтобы с нуля считать новый расход.
            fuelBase = tankCap; 
            curFuel = 0;
            LOG_INFO(name, "Full tank: %.1f L", fuelBase);
            break;

        // Команда: корректировка одометра.
        case CMD_CORRECT_ODO:
            // Установка базового значения пробега (odoBase) на новое заданное значение.
            odoBase = (double)cmd.value;
            LOG_INFO(name, "ODO corrected: %.0f", odoBase);
            break;

        // Команда не распознана.
        default:
            break;
    }
}

/**
 * @brief Обработка входящих данных.
 * 
 * Обрабатывает данные из трёх топиков:
 * - Topic::SENSOR: обновление текущего пробега, расхода, статуса двигателя
 * - Topic::CALCULATOR: загрузка сохранённых значений при старте
 * - Topic::STORAGE: обновление настроек (ёмкость бака)
 * 
 * @param topic Идентификатор топика
 * @param data  Указатель на данные
 */
void Calculator::onData(uint16_t topic, const void* data) {
    // Обработка данных по топику.
    switch (topic) {
        
        // Данные от датчиков двигателя (скорость, обороты и т.д.)
        case Topic::SENSOR: {
            // Приведение указателя данных к типу EnginePack.
            const EnginePack* p = (const EnginePack*)data;

            // Обрабо��ка события: двигатель только что запущен.
            if (p->engine_running && !engineRunning) {
                // Сброс текущих данных для новой поездки.
                curDist = 0; 
                curFuel = 0; 
                avgCur = 0;
                LOG_INFO(name, "Engine started");
            }

            // Обработка события: двигатель только что остановлен.
            if (!p->engine_running && engineRunning) {
                // Обновление накопленного среднего расхода.
                // Если это первая поездка, avgTotal = avgCur, иначе среднее между старым и новым.
                avgTotal = (avgTotal == 0) ? avgCur : (avgTotal + avgCur) / 2;
                LOG_INFO(name, "Engine stopped — avg=%.1f", avgTotal);

                // При остановке двигателя необходимо немедленно сохранить все данные.
                // Это делается публикацией полного пакета TripPack в топик CALCULATOR,
                // который модуль Storage подписан и сохранит его во Flash.
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
                pack.fuel_level      = notFuel ? fmaxf(0.0f, fuelBase - curFuel) : 0;
                pack.avg_consumption = (curDist > 0.001f) ? (curFuel / curDist) * 100.0f : 0;
                pack.avg_total       = avgTotal;
                publish(Topic::CALCULATOR, &pack, sizeof(pack));
            }

            // Обновление текущих значений на основе данных с датчика.
            engineRunning = p->engine_running;
            curDist = p->distance;
            curFuel = p->fuel_used;
            notFuel = p->not_fuel;
            break;
        }

        // Данные из топика CALCULATOR (сохранённые ранее значения при загрузке)
        case Topic::CALCULATOR: {
            // Приведение указателя данных к типу TripPack.
            const TripPack* p = (const TripPack*)data;

            // Загрузка базовых значений (пробег, сбросы поездок) при старте системы.
            // Условие p->odo > 0.01 проверяет, что данные не пустые (не только нули).
            if (!baseLoaded && p->odo > 0.01) {
                odoBase   = p->odo;
                tripABase = p->trip_a; fuelABase = p->fuel_trip_a;
                tripBBase = p->trip_b; fuelBBase = p->fuel_trip_b;
                if (p->avg_total > 0) avgTotal = p->avg_total;
                baseLoaded = true;
                LOG_INFO(name, "Base: ODO=%.0f, tripA=%.1f, fuel=%.1f", odoBase, tripABase, p->fuel_level);
            }

            // Загрузка уровня топлива, который был сохранён при последнем выключении системы.
            if (!fuelLoaded && p->fuel_level > 0.01f) {
                fuelBase = p->fuel_level; 
                fuelLoaded = true;
                LOG_INFO(name, "Fuel: %.1f L", fuelBase);
            }
            break;
        }

        // Новые настройки из топика STORAGE (например, ёмкость бака)
        case Topic::STORAGE: {
            // Приведение указателя данных к типу SettingsPack.
            const SettingsPack* p = (const SettingsPack*)data;

            // Обработка новых настроек.
            if (!settingsLoaded) {
                settingsLoaded = true;
                LOG_INFO(name, "Settings: tank=%.1f", p->tank_capacity);
            }

            // Обновление ёмкости бака.
            float old = tankCap;
            tankCap = p->tank_capacity;
            // Если ёмкость бака уменьшилась, и текущий базовый уровень топлива больше новой ёмкости,
            // необходимо скорректировать базовый уровень.
            if (old != tankCap && fuelBase > tankCap) {
                fuelBase = tankCap;
            }
            break;
        }
    }
}

/**
 * @brief Периодическая обработка.
 * 
 * Раз в секунду публикует обновлённые данные в Topic::CALCULATOR,
 * если загружены базовые значения. Используется для телеметрии.
 */
void Calculator::onProcess() {
    // Данные не публикуются, пока не загружены базовые значения из Storage.
    if (!baseLoaded) return;

    unsigned long now = millis();
    // Публикация раз в 1000 мс.
    if (now - lastPublish >= 1000) {
        lastPublish = now;

        // Формирование пакета данных для публикации.
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
        pack.fuel_level      = notFuel ? fmaxf(0.0f, fuelBase - curFuel) : 0;
        // Расчёт текущего расхода (л/100км).
        pack.avg_consumption = (curDist > 0.001f) ? (curFuel / curDist) * 100.0f : 0;
        // Сохранение значения текущего расхода в переменную avgCur.
        avgCur = pack.avg_consumption;
        pack.avg_total       = avgTotal;

        // Публикация пакета данных в топик CALCULATOR.
        // Другие модули (например, Protocol) подписаны на этот топик и получат данные.
        publish(Topic::CALCULATOR, &pack, sizeof(pack));
    }
}