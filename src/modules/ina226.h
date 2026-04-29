/**
 * @file ina226.h
 * @brief Драйвер INA226 — прецизионный измеритель напряжения и тока (I2C).
 * 
 * Подключение:
 * - SDA/SCL — общая I2C шина (GPIO 21/22)
 * - Адрес: 0x40 (A0=GND, A1=GND)
 * 
 * Используется для точного измерения напряжения бортсети автомобиля.
 * Диапазон: 0–36 В, точность: ±1.25 мВ.
 */

#ifndef INA226_H
#define INA226_H

#include <Arduino.h>
#include <Wire.h>

class INA226 {
public:
    /**
     * @brief Конструктор.
     * @param addr I2C адрес (0x40 при A0=GND, A1=GND)
     */
    explicit INA226(uint8_t addr = 0x40);

    /**
     * @brief Инициализация I2C и проверка подключения.
     * @param wire Шина I2C (по умолчанию Wire)
     * @param shunt_ohm Сопротивление шунта в Омах (по умолчанию 0.1)
     * @return true если INA226 обнаружена и настроена
     */
    bool begin(TwoWire& wire = Wire, float shunt_ohm = 0.1f);

    /**
     * @brief Чтение напряжения на шине (бортсеть).
     * @return Напряжение в милливольтах (мВ)
     */
    float readBusVoltage();

    /**
     * @brief Чтение напряжения на шунте.
     * @return Напряжение в милливольтах (мВ), диапазон ±81.92 мВ
     */
    float readShuntVoltage();

    /**
     * @brief Чтение тока через шунт.
     * @return Ток в миллиамперах (мА)
     */
    float readCurrent();

    /**
     * @brief Настройка калибровки.
     * @param shunt_ohm Сопротивление шунта в Омах
     */
    void configure(float shunt_ohm = 0.1f);

private:
    uint8_t _addr;
    TwoWire* _wire;
    float _shuntOhm;
    float _currentLSB_mA;  // мА на единицу

    static constexpr float SHUNT_LSB_uV = 2.5f;   // 2.5 мкВ на единицу
    static constexpr float BUS_LSB_mV   = 1.25f;  // 1.25 мВ на единицу

    uint16_t readRegister(uint8_t reg);
    void writeRegister(uint8_t reg, uint16_t value);
};

#endif
