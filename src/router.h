/**
 * @file router.h
 * @brief Центральная шина сообщений (event bus) для обмена данными между модулями.
 * 
 * Router реализует паттерн Publish-Subscribe, позволяя модулям подписываться
 * на определённые топики (topics) и получать данные при их публикации.
 * 
 * Особенности:
 * - Поддерживает до 16 топиков (битовая маска)
 * - До 6 подписчиков на один топик
 * - Хранит последнее значение (retain) для новых подписчиков
 * - Использует очереди FreeRTOS для асинхронной доставки
 * 
 * Используется всеми модулями для передачи данных и команд.
 */

#ifndef ROUTER_H
#define ROUTER_H

#include "core/module.h"
#include "core/command_msg.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/**
 * @def ROUTER_MAX_SUBS
 * @brief Максимальное количество подписчиков на один топик.
 */
#define ROUTER_MAX_SUBS 16

/**
 * @def ROUTER_RETAIN_SIZE
 * @brief Максимальный размер данных, сохраняемых для нового подписчика (retain).
 */
#define ROUTER_RETAIN_SIZE 128

/**
 * @class Router
 * @brief Класс центральной шины сообщений.
 * 
 * Отвечает за маршрутизацию данных между модулями через топики.
 * Подписчики получают данные через свои очереди.
 */
class Router {
public:
    /**
     * @brief Инициализация шины сообщений.
     * 
     * Вызывается один раз при старте системы.
     */
    void init();

    /**
     * @brief Подписка модуля на топик.
     * 
     * @param module Указатель на модуль-подписчик
     * @param topic Идентификатор топика (из enum Topic)
     * @param queue Очередь FreeRTOS для приёма данных
     * @param elemSize Размер одного элемента в очереди
     * @param depth Глубина очереди (1 = OVERWRITE, >1 = SEND)
     */
    void subscribe(Module* module, uint16_t topic, QueueHandle_t queue,
                   size_t elemSize, uint8_t depth);

    /**
     * @brief Публикация данных в топик.
     * 
     * Отправляет данные всем подписчикам и сохраняет копию (retain).
     * 
     * @param topic Идентификатор топика
     * @param data Указатель на данные
     * @param size Размер данных в байтах
     */
    void publish(uint16_t topic, const void* data, size_t size);

private:
    /**
     * @struct SubSlot
     * @brief Информация о подписчике.
     */
    struct SubSlot {
        QueueHandle_t queue;    ///< Очередь для приёма сообщений
        uint8_t       depth;      ///< Глубина очереди (1 или больше)
        size_t        elemSize;   ///< Размер элемента в очереди
    };

    /**
     * @struct TopicEntry
     * @brief Описание одного топика и его подписчиков.
     */
    struct TopicEntry {
        SubSlot subs[ROUTER_MAX_SUBS];  ///< Массив подписчиков
        uint8_t count;                  ///< Текущее количество подписчиков
        uint8_t retainData[ROUTER_RETAIN_SIZE]; ///< Сохранённые данные (retain)
        size_t  retainSize;             ///< Размер сохранённых данных
        bool    retainValid;            ///< Флаг: есть ли актуальные данные
    };

    TopicEntry _topics[16];  // индекс = номер бита (0..15)

    /**
     * @brief Получает индекс топика по его битовой маске.
     * 
     * @param topic Битовая маска топика (например, Topic::SENSOR)
     * @return Индекс в массиве _topics (0..15), или -1 если не найден
     */
    static int topicIndex(uint16_t topic);
};

#endif