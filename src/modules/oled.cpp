/**
 * @file oled.cpp
 * @brief Реализация модуля OLED дисплея SSD1306 128x64.
 * 
 * Отображает:
 * - Строка 1: статус двигателя (ENG:RUN / ENG:OFF)
 * - Строка 2: скорость (км/ч)
 * - Строка 3: обороты (об/мин)
 * - Строка 4: уровень топлива + прогресс-бар
 * - Строка 5: средний расход (л/100км)
 * - Строка 6: расход за поездку (л)
 * - Правый верхний угол: иконки габаритов и Bluetooth
 */

#include "oled.h"

#if OLED_ENABLED

#include "core/message.h"
#include "core/logging.h"
#include "core/version.h"
#include "core/icons.h"

/**
 * @brief Инициализация дисплея и подписка на топики.
 * 
 * Подписывается на:
 * - Topic::SENSOR — скорость, обороты, расход за поездку
 * - Topic::CALCULATOR — средний расход, уровень топлива
 * 
 * Выводит приветственный экран с названием и версией прошивки.
 */
bool Oled::onInit() {
    subscribeNew(Topic::SENSOR,     sizeof(EnginePack));
    subscribeNew(Topic::CALCULATOR, sizeof(TripPack));
    
    Wire.begin(21, 22);
    _u8g2.begin();
    _u8g2.setPowerSave(0);
    
    // Приветственный экран
    _u8g2.clearBuffer();
    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.setCursor(0, 10); _u8g2.print("TripComputer");
    _u8g2.setCursor(0, 25); _u8g2.print("ESP32 + OLED");
    _u8g2.setCursor(0, 40); _u8g2.print("Engine + K-Line");
    _u8g2.setCursor(0, 55); _u8g2.print(FW_VERSION_STR);
    _u8g2.sendBuffer();
    
    LOG_INFO(name, "Init done");
    return true;
}

/**
 * @brief Приём данных от сенсоров и вычислителя.
 * 
 * Обновляет локальный кэш для последующей отрисовки.
 * Данные приходят асинхронно, дисплей обновляется каждые 200 мс.
 */
void Oled::onData(uint16_t topic, const void* data) {
    switch (topic) {
        case Topic::SENSOR: {
            const EnginePack* p = (const EnginePack*)data;
            _speed          = p->speed;
            _rpm            = p->rpm;
            _voltage        = p->voltage;
            _engineRunning  = p->engine_running;
            _parkingLights  = p->parking_lights;
            _fuelUsed       = p->fuel_used;     // Расход за текущую поездку
            break;
        }
        case Topic::CALCULATOR: {
            const TripPack* p = (const TripPack*)data;
            _fuel         = p->fuel_level;
            _consumption  = p->avg_consumption;
            break;
        }
    }
}

/**
 * @brief Отслеживание статуса Bluetooth.
 * 
 * При изменении статуса транспорта обновляет иконку Bluetooth.
 */
void Oled::onCommand(const CommandMsg& cmd) {
    if (cmd.cmd == CMD_TRANSPORT_STATUS) {
        _btConnected = (cmd.value != 0);
    }
}

/**
 * @brief Периодическое обновление дисплея (каждые 200 мс).
 */
void Oled::onProcess() {
    unsigned long now = millis();
    if (now - _lastUpdate >= UPDATE_MS) {
        _lastUpdate = now;
        updateDisplay();
    }
}

/**
 * @brief Полная перерисовка экрана.
 * 
 * Формат экрана (128x64, шрифт 6x10):
 * ┌──────────────────────────────────────────┐
 * │ ENG:RUN                       🔆  🛜   │
 * │ SPD: 88.5 km/h                          │
 * │ RPM: 2200                               │
 * │ FUEL: 51.3L  [████████░░░░]             │
 * │ AVG: 8.5 L/100km                        │
 * │ TRIP: 2.47 L                            │
 * └──────────────────────────────────────────┘
 */
void Oled::updateDisplay() {
    _u8g2.clearBuffer();
    _u8g2.setFont(u8g2_font_6x10_tr);
    
    // Строка 1: статус двигателя
    _u8g2.setCursor(0, 10);
    _u8g2.print(_engineRunning ? "ENG:RUN" : "ENG:OFF");
    
    // Строка 2: скорость
    _u8g2.setCursor(0, 20);
    _u8g2.print("SPD:");
    _u8g2.print(_speed, 1);
    _u8g2.print(" km/h");
    
    // Строка 3: обороты
    _u8g2.setCursor(0, 30);
    _u8g2.print("RPM:");
    _u8g2.print((int)_rpm);
    
    // Строка 4: топливо + прогресс-бар
    _u8g2.setCursor(0, 40);
    _u8g2.print("FUEL:");
    _u8g2.print(_fuel, 1);
    _u8g2.print("L");
    drawProgressBar(75, 34, 50, 5, (_tankCapacity > 0) ? (_fuel / _tankCapacity) : 0);
    
    // Строка 5: средний расход
    _u8g2.setCursor(0, 50);
    _u8g2.print("AVG:");
    _u8g2.print(_consumption, 1);
    _u8g2.print(" L/100");
    
    // Строка 6: расход за поездку
    _u8g2.setCursor(0, 60);
    _u8g2.print("TRIP:");
    _u8g2.print(_fuelUsed, 2);
    _u8g2.print(" L");
    
    // Иконки в правом верхнем углу
    if (_parkingLights) {
        _u8g2.drawBitmap(96, 0, 2, 16, ic_parking_lights);
    }
    _u8g2.drawBitmap(112, 0, 2, 16, _btConnected ? ic_bt_connected : ic_bt_disconnected);
    
    _u8g2.sendBuffer();
}

/**
 * @brief Отрисовка прогресс-бара (уровень топлива).
 * 
 * @param x, y, w, h — координаты и размеры прямоугольника
 * @param pct — процент заполнения (0.0 = пусто, 1.0 = полно)
 */
void Oled::drawProgressBar(int x, int y, int w, int h, float pct) {
    pct = constrain(pct, 0.0f, 1.0f);
    _u8g2.drawFrame(x, y, w, h);
    int fw = (int)(w * pct);
    if (fw > 0) _u8g2.drawBox(x, y, fw, h);
}

#endif // OLED_ENABLED