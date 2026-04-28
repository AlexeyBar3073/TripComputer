/**
 * @file module.h
 * @brief Базовый класс для всех модулей системы.
 * 
 * Класс Module предоставляет общую функциональность для всех компонентов системы:
 * - Инициализацию с автоматическим созданием очереди команд
 * - Обработку команд и данных через хуки (onCommand, onData)
 * - Подписку на топики и публикацию сообщений через Router
 * - Корректное освобождение ресурсов в деструкторе
 * 
 * Все модули (Transport, Calculator, Protocol и др.) наследуются от Module.
 */

#ifndef MODULE_H
#define MODULE_H

#include "command_msg.h"
#include "logging.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/** @brief Максимальное количество подписок, которые может иметь один модуль */
#define MODULE_MAX_SUBS 6

// Предварительное объявление
class Router;

class Module {
public:
    /** @brief Имя модуля для логирования */
    const char* name;

    /**
     * @brief Конструктор модуля.
     * @param moduleName Уникальное имя модуля (для логов)
     */
    explicit Module(const char* moduleName)
        : name(moduleName), _router(nullptr), _cmdQueue(nullptr), _subCount(0)
    {
        memset(_subs, 0, sizeof(_subs));
    }

    /**
     * @brief Деструктор. Освобождает все созданные очереди FreeRTOS.
     * 
     * Важно: очереди удаляются только если модуль больше не используется.
     * В статической прошивке деструктор никогда не вызывается.
     */
    virtual ~Module() {
        if (_cmdQueue) vQueueDelete(_cmdQueue);
        for (uint8_t i = 0; i < _subCount; i++) {
            if (_subs[i].queue) vQueueDelete(_subs[i].queue);
        }
    }

    /**
     * @brief Инициализация модуля.
     * 
     * Создаёт очередь команд, подписывает на Topic::SYSTEM, вызывает onInit().
     * @param router Указатель на центральную шину сообщений
     * @return true при успехе, false при ошибке
     */
    bool init(Router* router);

    /**
     * @brief Основной цикл обработки модуля.
     * 
     * Порядок вызова:
     * 1. onProcess() — фоновая работа (расчёты, симуляция)
     * 2. onCommand() — обработка всех поступивших команд
     * 3. onData() — обработка данных из подписанных топиков
     */
    void process();

protected:
    Router*       _router;       ///< Указатель на шину сообщений
    QueueHandle_t _cmdQueue;     ///< Очередь для приёма команд

    // --- Хуки для переопределения в наследниках ---

    /** @brief Вызывается в конце init(). Здесь модуль создаёт свои очереди и подписывается */
    virtual bool onInit() { return true; }

    /** @brief Вызывается в начале process(). Фоновая работа модуля */
    virtual void onProcess() {}

    /** @brief Вызывается при получении команды из _cmdQueue */
    virtual void onCommand(const CommandMsg& cmd) {}

    /** @brief Вызывается при получении данных из подписанного топика */
    virtual void onData(uint16_t topic, const void* data) {}

    // --- Методы для наследников ---

    /** @brief Подписаться на топик с уже созданной очередью */
    bool subscribe(uint16_t topic, QueueHandle_t queue, size_t elemSize, uint8_t depth = 1);

    /** @brief Создать OVERWRITE-очередь и подписаться на топик */
    QueueHandle_t subscribeNew(uint16_t topic, size_t elemSize);

    /** @brief Опубликовать данные в топик через Router */
    bool publish(uint16_t topic, const void* data, size_t size);

private:
    struct SubEntry {
        uint16_t      topic;    ///< Идентификатор топика
        QueueHandle_t queue;    ///< Очередь для приёма
        uint8_t       depth;    ///< Глубина очереди (1 = OVERWRITE)
    };

    SubEntry _subs[MODULE_MAX_SUBS];  ///< Массив подписок
    uint8_t  _subCount;               ///< Текущее количество подписок
};

#endif