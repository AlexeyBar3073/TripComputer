/**
 * @file main.cpp
 * @brief Точка входа и инициализация системы TripComputer.
 * 
 * Этот файл содержит функцию setup() и loop() — стандартные для Arduino-приложений,
 * а также реализацию задач FreeRTOS для обработки различных компонентов системы.
 * 
 * Основные задачи:
 * - transportProtocolTask: обработка Bluetooth и протокола обмена
 * - engineTask: симуляция работы двигателя и датчиков
 * - klineTask: эмуляция данных с шины K-Line
 * - mainTask: основная логика (расчёты, хранение, климат)
 * 
 * Все модули инициализируются через общий Router, который обеспечивает обмен сообщениями.
 */

#include <Arduino.h>
#include "router.h"
#include "modules/transport.h"
#include "modules/protocol.h"
#include "modules/simulator.h"
#include "modules/calculator.h"
#include "modules/storage.h"
#include "modules/kline.h"
#include "modules/climate.h"
#include "modules/ota.h"

// Глобальные экземпляры модулей системы
Router     router;        ///< Центральная шина сообщений
Transport  transport;     ///< Модуль Bluetooth-связи
Protocol   protocol;      ///< Модуль обработки команд и телеметрии
Simulator  simulator;     ///< Модуль симуляции датчиков двигателя
Calculator calculator;   ///< Модуль расчёта расхода, пробега и средних значений
Storage    storage;       ///< Модуль хранения данных в Flash
KLine      kline;         ///< Модуль эмуляции K-Line данных
Climate    climate;       ///< Модуль климат-системы (температура, давление)
Ota        ota;          ///< Модуль OTA-обновления

/**
 * @brief Задача FreeRTOS: обработка Bluetooth и протокола.
 * 
 * Выполняется каждые 10 мс на ядре 0. Отвечает за:
 * - Приём и отправку данных по Bluetooth (transport)
 * - Парсинг входящих команд и формирование ответов (protocol)
 */
void transportProtocolTask(void*) {
    while (1) {
        transport.process();
        protocol.process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Задача FreeRTOS: симуляция работы двигателя.
 * 
 * Выполняется каждые 10 мс на ядре 1. Обновляет данные симулятора:
 * - Скорость, обороты, расход топлива
 * - Имитация нажатия кнопок (запуск двигателя, габариты)
 * - Чтение потенциометра (педаль газа)
 */
void engineTask(void*) {
    while (1) {
        simulator.process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Задача FreeRTOS: эмуляция K-Line данных.
 * 
 * Выполняется каждые 50 мс на ядре 1. Генерирует:
 * - Температуру ОЖ и АКПП
 * - Положение селектора и передачу
 * - Коды ошибок (DTC)
 * - Напряжение бортсети
 */
void klineTask(void*) {
    while (1) {
        kline.process();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief Задача FreeRTOS: основная логика приложения.
 * 
 * Выполняется каждые 10 мс на ядре 0. Обновляет:
 * - Расчёты расхода и пробега (calculator)
 * - Сохранение данных (storage)
 * - Данные климат-системы (climate)
 */
void mainTask(void*) {
    while (1) {
        calculator.process();
        storage.process();
        climate.process();
        ota.process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Инициализация системы при старте.
 * 
 * Выполняет:
 * - Запуск Serial для отладки
 * - Инициализацию всех модулей через Router
 * - Создание задач FreeRTOS
 */
void setup() {
    Serial.begin(115200);
    delay(5000);
    
    router.init();
    
    storage.init(&router);
    calculator.init(&router);
    simulator.init(&router);
    kline.init(&router);
    climate.init(&router);
    protocol.init(&router);
    transport.init(&router);
    ota.init(&router); 
    
    xTaskCreatePinnedToCore(transportProtocolTask, "BtRx",   4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(engineTask,             "Engine", 3072, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(klineTask,              "KLine",  3072, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(mainTask,               "Main",   4096, NULL, 2, NULL, 0);
}

/**
 * @brief Пустая функция loop.
 * 
 * Вся логика выполняется в задачах FreeRTOS, поэтому loop() не используется.
 * Просто ждёт 1 секунду в цикле.
 */
void loop() { delay(1000); }