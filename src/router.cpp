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
    // Обнуление массива _topics, который содержит информацию о всех 16 топиках.
    // Это сбрасывает количество подписчиков, сохранённые данные (retain) и другие поля.
    memset(_topics, 0, sizeof(_topics));
    
    // Запись информационного сообщения в лог, подтверждающего успешную инициализацию.
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
 * @param depth Глубина о��ереди (1 = OVERWRITE, >1 = SEND)
 */
void Router::subscribe(Module* module, uint16_t topic, QueueHandle_t queue,
                        size_t elemSize, uint8_t depth) {
    // Преобразование битовой маски топика в индекс массива _topics (0-15).
    // Например, Topic::SENSOR (0x01) -> индекс 0, Topic::CALCULATOR (0x02) -> индекс 1.
    int idx = topicIndex(topic);
    // Проверка корректности индекса.
    if (idx < 0 || idx >= 16) return;

    // Получение ссылки на соответствующую запись в массиве топиков.
    TopicEntry& te = _topics[idx];
    // Проверка, не превышено ли максимальное количество подписчиков для этого топика.
    if (te.count >= ROUTER_MAX_SUBS) return;

    // Регистрация нового подписчика в массиве.
    te.subs[te.count].queue    = queue;       // Ссылка на очередь FreeRTOS модуля.
    te.subs[te.count].depth    = depth;       // Глубина очереди: 1 (OVERWRITE) или >1 (SEND).
    te.subs[te.count].elemSize = elemSize;    // Размер одного элемента данных, который будет получать модуль.
    te.count++; // Увеличение счётчика подписчиков.

    // Проверка, есть ли для этого топика сохранённые данные (retain) от предыдущих публикаций.
    if (te.retainValid) {
        // Если у подписчика глубина очереди 1, используется xQueueOverwrite,
        // что гарантирует, что он получит актуальные данные.
        if (depth == 1)
            xQueueOverwrite(queue, te.retainData);
        // Если глубина больше 1, используется xQueueSend, который помещает данные в конец очереди.
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
    // Преобразование битовой маски топика в индекс массива _topics.
    int idx = topicIndex(topic);
    if (idx < 0 || idx >= 16) return;
    
    // Получение ссылки на запись топика.
    TopicEntry& te = _topics[idx];
    
    // Если размер данных не превышает лимит, они сохраняются для новых подписчиков.
    if (size <= ROUTER_RETAIN_SIZE) {
        memcpy(te.retainData, data, size); // Копирование данных в буфер retain.
        te.retainSize = size;             // Сохранение размера.
        te.retainValid = true;            // Установка флага, что данные актуальны.
    }
    
    // Специальная обработка публикации в топик SYSTEM (команды).
    // Это позволяет видеть в логе, какая команда отправляется и скольким модулям.
    if (topic == Topic::SYSTEM) {
        const CommandMsg* c = (const CommandMsg*)data;
        LOG_DEBUG("ROUTER", "Pub SYSTEM: cmd=0x%02X, val=%.0f, subs=%d", c->cmd, c->value, te.count);
    }
    
    // Цикл по всем зарегистрированным подписчикам топика.
    for (uint8_t i = 0; i < te.count; i++) {
        // Пропуск пустых слотов (защита от нулевых указателей).
        if (!te.subs[i].queue) continue;
        
        // Отправка данных в очередь подписчика.
        // Используется xQueueOverwrite для глубины 1 и xQueueSend для глубины >1.
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
    // Цикл по 16 возможным битам (от 0 до 15).
    for (int i = 0; i < 16; i++) {
        // Проверка, установлен ли i-й бит в маске топика.
        if (topic & (1 << i)) return i;
    }
    // Если ни один бит не установлен, возвращается -1 (ошибка).
    return -1;
}