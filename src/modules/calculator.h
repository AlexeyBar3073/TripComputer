/**
 * @file calculator.h
 * @brief Модуль расчёта расхода топлива, пробега и средних значений.
 * 
 * Класс Calculator отвечает за:
 * - Подсчёт текущего и среднего расхода топлива
 * - Управление сбросом поездок A и B
 * - Корректировку одометра
 * - Обновление данных на основе показаний датчиков
 * - Взаимодействие с модулем Storage для сохранения состояния
 * 
 * Работает на основе данных из топиков:
 * - Topic::SENSOR — текущие показания датчиков
 * - Topic::STORAGE — настройки (ёмкость бака и др.)
 * - Topic::CALCULATOR — сохранённые ранее значения
 */

#ifndef CALCULATOR_H
#define CALCULATOR_H

#include "core/module.h"
#include "core/packets.h"

/**
 * @class Calculator
 * @brief Модуль расчёта топливной экономичности и пробега.
 * 
 * Выполняет расчёт среднего расхода, управляет поездками A/B,
 * корректирует одометр и обновляет данные каждую секунду.
 */
class Calculator : public Module {
public:
    /**
     * @brief Конструктор модуля.
     * 
     * Устанавливает имя модуля как "Calculator" для логирования.
     */
    Calculator() : Module("Calculator") {}

protected:
    /**
     * @brief Инициализация модуля.
     * 
     * Подписывается на топики:
     * - Topic::SENSOR     — для получения данных с датчиков
     * - Topic::CALCULATOR — для загрузки сохранённых значений
     * - Topic::STORAGE    — для получения настроек (ёмкость бака)
     * 
     * @return true — всегда успешна
     */
    bool onInit() override;

    /**
     * @brief Периодическая обработка.
     * 
     * Раз в секунду публикует обновлённые данные в Topic::CALCULATOR.
     * Выполняется только после загрузки базовых значений из Storage.
     */
    void onProcess() override;

    /**
     * @brief Обработка входящих команд.
     * 
     * Поддерживает команды:
     * - CMD_RESET_TRIP_A — сброс поездки A
     * - CMD_RESET_TRIP_B — сброс поездки B
     * - CMD_RESET_AVG    — сброс среднего расхода
     * - CMD_FULL_TANK    — установка полного бака
     * - CMD_CORRECT_ODO  — корректировка одометра
     * 
     * @param cmd Команда (код + значение)
     */
    void onCommand(const CommandMsg& cmd) override;

    /**
     * @brief Обработка входящих данных.
     * 
     * Реагирует на данные из топиков:
     * - Topic::SENSOR     — обновление текущего пробега и расхода
     * - Topic::CALCULATOR — загрузка сохранённых значений при старте
     * - Topic::STORAGE    — обновление настроек (ёмкость бака)
     * 
     * @param topic Идентификатор топика
     * @param data  Указатель на данные
     */
    void onData(uint16_t topic, const void* data) override;

private:
    // === Базовые значения из Storage ===
    double odoBase = 0;           ///< Базовый пробег (для расчёта общего)
    float  tripABase = 0, tripBBase = 0; ///< Базовые значения поездок A и B
    float  fuelABase = 0, fuelBBase = 0; ///< Базовые значения расхода по поездкам
    float  fuelBase = 60;         ///< Базовый уровень топлива (при полном баке)
    bool   baseLoaded = false;     ///< Флаг: загружены ли базовые значения
    bool   fuelLoaded = false;     ///< Флаг: загружен ли уровень топлива

    // === Данные текущей поездки ===
    float  curDist = 0, curFuel = 0; ///< Текущий пробег и расход

    // === Средний расход ===
    float  avgCur = 0, avgTotal = 0; ///< Текущий и накопленный средний расход (л/100км)

    // === Статус двигателя ===
    bool   engineRunning = false;  ///< Флаг: двигатель работает
    bool   notFuel = true;         ///< Флаг: датчик топлива отсутствует

    // === Настройки ===
    float  tankCap = 60;           ///< Ёмкость бака (из настроек)
    bool   settingsLoaded = false; ///< Флаг: загружены ли настройки

    // === Таймер публикации ===
    unsigned long lastPublish = 0; ///< Время последней публикации (мс)
};

#endif