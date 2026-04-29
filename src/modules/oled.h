/**
 * @file oled.h
 * @brief Модуль отображения информации на OLED дисплее SSD1306 128x64.
 * 
 * Подписывается на EnginePack, TripPack, статус транспорта.
 * Отображает скорость, обороты, топливо, расход, статус BT и габаритов.
 * Обновляется каждые 200 мс.
 * 
 * Аппаратная конфигурация:
 * - SDA = GPIO 21, SCL = GPIO 22
 * - Дисплей: SSD1306 128x64, аппаратный I2C (U8g2)
 */

#ifndef OLED_H
#define OLED_H

#if OLED_ENABLED

#include "core/module.h"
#include "core/packets.h"
#include <U8g2lib.h>
#include <Wire.h>

class Oled : public Module {
public:
    Oled() : Module("OLED") {}

protected:
    bool onInit() override;
    void onProcess() override;
    void onData(uint16_t topic, const void* data) override;
    void onCommand(const CommandMsg& cmd) override;

private:
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C _u8g2{U8G2_R0, U8X8_PIN_NONE};
    
    // Кэш данных
    float _speed = 0;
    float _rpm = 0;
    float _fuel = 0;
    float _voltage = 12.7f;
    float _consumption = 0;
    float _fuelUsed = 0;       ///< Расход за текущую поездку (л)
    bool  _engineRunning = false;
    bool  _btConnected = false;
    bool  _parkingLights = false;
    float _tankCapacity = 60.0f;
    
    unsigned long _lastUpdate = 0;
    static constexpr unsigned long UPDATE_MS = 200;
    
    void updateDisplay();
    void drawProgressBar(int x, int y, int w, int h, float pct);
};

#endif // OLED_ENABLED
#endif // OLED_H