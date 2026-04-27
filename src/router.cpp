/**
 * @file router.cpp
 * @brief Реализация центральной шины сообщений (Router).
 * 
 * Содержит методы для инициализации, подписки и публикации данных в топики.
 * Использует FreeRTOS очереди для доставки сообщений между модулями.
 */

#include "router.h"
#include "core/logging.h"
#include "core/message.h" 

/**
 * @brief Инициализация шины сообщений.
 * 
 * Обнуляет все топики и их подписчиков. Выводит сообщение в лог.
 */
void Router::init() {
    memset(_topics, 0, sizeof(_topics));
    LOG_INFO("ROUTER", "Init done");
}

/**
 * @brief Подписка модуля на топик.
 * 
 * Добавляет модуль в список подписчиков указанного топика.
 * Если уже есть сохранённые данные (retain), отправляет их сразу.
 * 
 * @param module Указатель на модуль-подписчик (для логирования)
 * @param topic Идентификатор топика (битовая маска)
 * @param queue Очередь FreeRTOS для приёма сообщений
 * @param elemSize Размер одного элемента данных
 * @param depth Глубина очереди (1 = OVERWRITE, >1 = SEND)
 */
void Router::subscribe(Module* module, uint16_t topic, QueueHandle_t queue,
                        size_t elemSize, uint8_t depth) {
    int idx = topicIndex(topic);
    if (idx < 0 || idx >= 16) return;

    TopicEntry& te = _topics[idx];
    if (te.count >= ROUTER_MAX_SUBS) return;

    te.subs[te.count].queue    = queue;
    te.subs[te.count].depth    = depth;
    te.subs[te.count].elemSize = elemSize;
    te.count++;

    // Если уже есть сохранённые данные — отправить новому подписчику
    if (te.retainValid) {
        if (depth == 1)
            xQueueOverwrite(queue, te.retainData);
        else
            xQueueSend(queue, te.retainData, 0);
    }
}

/**
 * @brief Публикация данных в топик.
 * 
 * Отправляет данные всем подписчикам топика.
 * Если размер данных <= ROUTER_RETAIN_SIZE, сохраняет копию (retain)
 * для новых подписчиков.
 * 
 * @param topic Идентификатор топика
 * @param data Указатель на данные
 * @param size Размер данных в байтах
 */
void Router::publish(uint16_t topic, const void* data, size_t size) {
    int idx = topicIndex(topic);
    if (idx < 0 || idx >= 16) return;
    
    TopicEntry& te = _topics[idx];
    
    if (size <= ROUTER_RETAIN_SIZE) {
        memcpy(te.retainData, data, size);
        te.retainSize = size;
        te.retainValid = true;
    }
    
    // ЛОГ для SYSTEM
    if (topic == Topic::SYSTEM) {
        const CommandMsg* c = (const CommandMsg*)data;
        LOG_DEBUG("ROUTER", "Pub SYSTEM: cmd=0x%02X, val=%.0f, subs=%d", c->cmd, c->value, te.count);
    }
    
    for (uint8_t i = 0; i < te.count; i++) {
        if (!te.subs[i].queue) continue;
        if (te.subs[i].depth == 1)
            xQueueOverwrite(te.subs[i].queue, data);
        else
            xQueueSend(te.subs[i].queue, data, 0);
    }
}

/**
 * @brief Получает индекс топика по его битовой маске.
 * 
 * Проходит по битам (0..15) и возвращает позицию установленного бита.
 * Используется для индексации массива _topics.
 * 
 * @param topic Битовая маска топика (например, Topic::SENSOR)
 * @return Индекс в массиве (0..15), или -1 если бит не найден
 */
int Router::topicIndex(uint16_t topic) {
    for (int i = 0; i < 16; i++) {
        if (topic & (1 << i)) return i;
    }
    return -1;
}