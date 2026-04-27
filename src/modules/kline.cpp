/**
 * @file kline.cpp
 * @brief Реализация модуля эмуляции K-Line данных.
 * 
 * Генерирует и публикует реалистичные данные, имитирующие
 * информацию от автомобильного ЭБУ по шине K-Line.
 */

#include "kline.h"
#include "core/message.h"
#include "core/logging.h"

/**
 * @brief Инициализация модуля KLine.
 * 
 * Устанавливает начальные значения в структуре KlinePack:
 * - Версия пакета: 2
 * - Температура ОЖ: 90°C
 * - Температура АКПП: 75°C
 * - Позиция селектора: D (3)
 * - Передача: 2
 * - Коды ошибок: P0135;P0141
 * 
 * @return true — всегда успешна
 */
bool KLine::onInit() {
    memset(&pack, 0, sizeof(pack));
    pack.version = 2;
    pack.coolant_temp = 90; 
    pack.atf_temp = 75;
    pack.selector_position = 3; 
    pack.current_gear = 2;
    strcpy(pack.dtc_codes, "P0135;P0141");
    return true;
}

/**
 * @brief Обработка входящих команд.
 * 
 * Реагирует на команды:
 * - CMD_KL_CLEAR_DTC: очищает список кодов неисправностей
 * - CMD_KL_DETECT_PROTO: выводит сообщение в лог (для диагностики)
 * 
 * @param cmd Команда от системы
 */
void KLine::onCommand(const CommandMsg& cmd) {
    switch (cmd.cmd) {
        case CMD_KL_CLEAR_DTC: 
            // Очистка кодов ошибок
            memset(pack.dtc_codes, 0, 64); 
            break;
        
        case CMD_KL_DETECT_PROTO: 
            // Команда диагностики протокола
            LOG_INFO(name, "Proto detect"); 
            break;
            
        default:
            break;
    }
}

/**
 * @brief Периодическая генерация данных K-Line.
 * 
 * Каждую секунду обновляет значения с небольшим случайным разбросом
 * для имитации реальных изменений и публикует их в Topic::KLINE.
 * 
 * Обновляемые параметры:
 * - Температура ОЖ: 85-87°C
 * - Температура АКПП: 70-71.5°C
 * - Напряжение бортсети: 14.0-14.5 В
 * - Уровень топлива: 55-57%
 * - Обороты выходного вала: 1500-2000 об/мин
 */
void KLine::onProcess() {
    unsigned long now = millis();
    if (now - lastPublish >= 1000) {
        lastPublish = now;
        
        // Генерация реалистичных значений с небольшим разбросом
        pack.coolant_temp = 85 + random(0,20)/10.0f;
        pack.atf_temp = 70 + random(0,15)/10.0f;
        pack.voltage = 14 + random(0,50)/100.0f;
        pack.fuel_percent = 55 + random(0,20)/10.0f;
        pack.output_shaft_rpm = 1500 + random(0,500);
        
        // Публикация обновлённых данных
        publish(Topic::KLINE, &pack, sizeof(pack));
    }
}