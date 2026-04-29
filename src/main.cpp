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
#include "modules/engine.h"
#include "modules/calculator.h"
#include "modules/storage.h"
#include "modules/kline.h"
#include "modules/climate.h"
#include "modules/ota.h"
#include "modules/oled.h"

#if OLED_ENABLED
Oled oled;
#endif

// Глобальные экземпляры модулей системы
Router     router;        ///< Центральная шина сообщений
Transport  transport;     ///< Модуль Bluetooth-связи
Protocol   protocol;      ///< Модуль обработки команд и телеметрии
Engine     engine;        ///< Модуль датчиков двигателя
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
        // 1. Обработка входящих и исходящих Bluetooth-данных.
        // Модуль transport отвечает за прием команд и отправку ответов через Bluetooth.
        transport.process();

        // 2. Парсинг JSON-команд и отправка системных команд.
        // Модуль protocol разбирает входящие JSON, выполняет свои команды (например, get_cfg) 
        // и отправляет остальные (например, reset_trip) в шину сообщений (Topic::SYSTEM).
        protocol.process();

        // Задержка на 10 мс для ограничения частоты выполнения цикла.
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
        // Обновление модели симуляции двигателя.
        // Симулятор генерирует данные (скорость, обороты, расход топлива) и публикует их в топик Topic::SENSOR.
        // Также обрабатывает нажатия кнопок запуска двигателя и включения габаритов.
        engine.process();

        // Задержка на 10 мс.
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
        // Получение данных с помощью протокола K-Line.
        // Если _realMode=true, модуль пытается подключиться к реальному ЭБУ по K-Line.
        // Если соединение установлено, он запрашивает данные (температуры, напряжение, коды ошибок).
        // Если _realMode=false, он генерирует случайные значения для тестирования.
        kline.process();

        // Задержка на 50 мс, так как K-Line работает медленно.
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
        // 1. Обновление расчетов (средний расход, пробег поездок A/B, уровень топлива).
        // Калькулятор обновляет свои данные на основе данных из Topic::SENSOR и публикует их в Topic::CALCULATOR.
        calculator.process();

        // 2. Сохранение важных данных (пробег, настройки) во Flash-память.
        // Модуль storage периодически проверяет, изменились ли данные, и при необходимости сохраняет их.
        storage.process();

        // 3. Генерация данных о климате (температура салона/улицы, давление в шинах, уровень омывателя).
        // Данные публикуются в Topic::SERVICE.
        climate.process();

        // 4. Обработка OTA-обновлений (прием чанков прошивки).
        // Проверяет, нет ли таймаута при приеме чанков.
        ota.process();

        #if OLED_ENABLED
        oled.process();
        #endif

        // Задержка на 10 мс.
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
    // Инициализация последовательного порта для отладки на скорости 115200 бод.
    Serial.begin(115200);
    
    // Задержка в 5 секунд для стабилизации и возможности подключения терминала.
    delay(5000);
    
    // Инициализация центральной шины сообщений (Router), которая будет использоваться для обмена данными между модулями.
    router.init();
    
    // Последовательная инициализация всех модулей системы. Каждый модуль регистрируется в Router и создает свои очереди.
    storage.init(&router);
    calculator.init(&router);
    engine.init(&router);
    kline.init(&router);
    climate.init(&router);
    protocol.init(&router);
    transport.init(&router);
    ota.init(&router); 
    #if OLED_ENABLED
    oled.init(&router);
    #endif

    // Создание задач FreeRTOS и привязка их к конкретным ядрам процессора для оптимизации производительности.
    // Задача transportProtocolTask (высокий приоритет) на ядре 0.
    xTaskCreatePinnedToCore(transportProtocolTask, "BtRx",   4096, NULL, 3, NULL, 0);
    // Задача engineTask (средний приоритет) на ядре 1.
    xTaskCreatePinnedToCore(engineTask,             "Engine", 3072, NULL, 2, NULL, 1);
    // Задача klineTask (низкий приоритет) на ядре 1.
    xTaskCreatePinnedToCore(klineTask,              "KLine",  3072, NULL, 1, NULL, 1);
    // Задача mainTask (средний приоритет) на ядре 0.
    xTaskCreatePinnedToCore(mainTask,               "Main",   4096, NULL, 2, NULL, 0);
}

/**
 * @brief Пустая функция loop.
 * 
 * Вся логика выполняется в задачах FreeRTOS, поэтому loop() не используется.
 * Просто ждёт 1 секунду в цикле.
 */
void loop() { 
    // Основная логика приложения полностью перенесена в задачи FreeRTOS.
    // Функция loop() остается пустой и просто создает небольшую задержку.
    delay(1000); 
}