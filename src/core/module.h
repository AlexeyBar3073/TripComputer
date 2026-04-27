/**
 * @file module.h
 * @brief Базовый класс для всех модулей системы.
 * 
 * Класс Module предоставляет общую функциональность для всех компонентов системы:
 * - Инициализацию
 * - Обработку команд и данных
 * - Подписку на топики
 * - Отправку сообщений
 * 
 * Все модули (Transport, Calculator и др.) наследуются от Module.
 */

#ifndef MODULE_H
#define MODULE_H

#include "command_msg.h"
#include "logging.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/**
 * @def MODULE_MAX_SUBS
 * @brief Максимальное количество подписок, которые может иметь модуль.
 */
#define MODULE_MAX_SUBS 6

/**
 * forward declaration
 */
class Router;

/**
 * @class Module
 * @brief Абстрактный базовый класс для всех модулей системы.
 * 
 * Реализует шаблон проектирования "Модуль" с хуками для переопределения.
 * Использует Router для обмена сообщениями.
 */
class Module {
public:
    /**
     * @brief Имя модуля для логирования.
     */
    const char* name;

    /**
     * @brief Конструктор модуля.
     * 
     * @param moduleName Уникальное имя модуля (для логирования)
     */
    explicit Module(const char* moduleName)
        : name(moduleName), _router(nullptr), _cmdQueue(nullptr), _subCount(0)
    {
        memset(_subs, 0, sizeof(_subs));
    }

    /**
     * @brief Виртуальный деструктор.
     */
    virtual ~Module() = default;

    /**
     * @brief Инициализация модуля.
     * 
     * Вызывается при старте системы. Создаёт очередь команд и вызывает onInit().
     * 
     * @param router Указатель на центральную шину сообщений
     * @return true при успехе, false при ошибке
     */
    bool init(Router* router);

    /**
     * @brief Основной цикл обработки.
     * 
     * Вызывается периодически из задачи FreeRTOS. Выполняет:
     * - onProcess() — фоновую работу
     * - onCommand() — обработку команд
     * - onData() — обработку входящих данных
     */
    void process();

protected:
    /**
     * @brief Указатель на шину сообщений.
     */
    Router*       _router;

    /**
     * @brief Очередь для приёма команд.
     */
    QueueHandle_t _cmdQueue;

    // --- Хуки (для переопределения в наследниках) ---

    /**
     * @brief Хук инициализации.
     * 
     * Вызывается при старте. Может быть переопределён.
     * 
     * @return true — успех, false — ошибка
     */
    virtual bool onInit() { return true; }

    /**
     * @brief Хук периодической обработки.
     * 
     * Вызывается каждые несколько миллисекунд. Используется для:
     * - Обновления симуляции
     * - Расчётов
     * - Генерации данных
     */
    virtual void onProcess() {}

    
    /**
     * @brief Хук обработки команд.
     * 
     * Вызывается при получении команды (например, сброс поездки).
     * 
     * @param cmd Структура команды (код + значение)
     */
    virtual void onCommand(const CommandMsg& cmd) {}

    
    /**
     * @brief Хук приёма данных.
     * 
     * Вызывается при получении данных из топика, на который модуль подписан.
     * 
     * @param topic Идентификатор топика
     * @param data Указатель на данные
     */
    virtual void onData(uint16_t topic, const void* data) {}

    // --- Методы для наследников ---

    /**
     * @brief Подписаться на топик с уже созданной очередью.
     * 
     * @param topic Идентификатор топика
     * @param queue Очередь FreeRTOS
     * @param elemSize Размер элемента
     * @param depth Глубина очереди (1 = OVERWRITE, >1 = SEND)
     * @return true — успех, false — ошибка
     */
    bool subscribe(uint16_t topic, QueueHandle_t queue, size_t elemSize, uint8_t depth = 1);

    /**
     * @brief Создать очередь и подписаться на топик.
     * 
     * Создаёт очередь типа OVERWRITE (глубина 1) и подписывает модуль.
     * 
     * @param topic Идентификатор топика
     * @param elemSize Размер элемента данных
     * @return Указатель на очередь, или nullptr при ошибке
     */
    QueueHandle_t subscribeNew(uint16_t topic, size_t elemSize);

    /**
     * @brief Опубликовать данные в топик.
     * 
     * Отправляет данные через Router.
     * 
     * @param topic Идентификатор топика
     * @param data Указатель на данные
     * @param size Размер данных
     * @return true — успех, false — ошибка
     */
    bool publish(uint16_t topic, const void* data, size_t size);

private:
    /**
     * @struct SubEntry
     * @brief Запись о подписке модуля.
     */
    struct SubEntry {
        uint16_t      topic;   ///< Идентификатор топика
        QueueHandle_t queue;   ///< Очередь для приёма
        uint8_t       depth;   ///< Глубина очереди
    };

    SubEntry _subs[MODULE_MAX_SUBS];  ///< Массив подписок
    uint8_t  _subCount;               ///< Текущее количество подписок
};

#endif