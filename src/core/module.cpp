/**
 * @file module.cpp
 * @brief Реализация базового класса Module.
 * 
 * Содержит методы инициализации, обработки и взаимодействия с Router.
 * Является основой для всех функциональных модулей системы.
 */

#include "module.h"
#include "router.h"
#include "message.h"

/**
 * @brief Инициализация модуля.
 * 
 * Выполняет:
 * - Сохранение указателя на Router
 * - Создание очереди команд (CommandMsg)
 * - Подписку на топик Topic::SYSTEM для приёма команд
 * - Вызов виртуального метода onInit()
 * 
 * @param router Указатель на центральную шину сообщений
 * @return true при успехе, false при ошибке (например, не создана очередь)
 */
bool Module::init(Router* router) {
    _router = router;

    _cmdQueue = xQueueCreate(5, sizeof(CommandMsg));
    if (!_cmdQueue) {
        LOG_ERROR(name, "Cmd queue create failed");
        return false;
    }

    _router->subscribe(this, Topic::SYSTEM, _cmdQueue, sizeof(CommandMsg), 5);

    return onInit();
}

/**
 * @brief Основной цикл обработки модуля.
 * 
 * Вызывается периодически из задачи FreeRTOS. Выполняет в строгом порядке:
 * 1. onProcess() — фоновая логика (расчёт, симуляция)
 * 2. onCommand() — обработка всех поступивших команд
 * 3. onData() — обработка данных из подписанных топиков
 * 
 * Используется для предотвращения блокировок и обеспечения предсказуемого поведения.
 */
void Module::process() {
    // 1. Сначала хук наследника (телеметрия, фоновая работа)
    onProcess();

    // 2. Потом команды
    CommandMsg cmd;
    while (xQueueReceive(_cmdQueue, &cmd, 0) == pdTRUE) {
        onCommand(cmd);
    }

    // 3. Потом данные подписок
    static uint8_t buf[512];
    for (uint8_t i = 0; i < _subCount; i++) {
        if (!_subs[i].queue) continue;
        if (xQueueReceive(_subs[i].queue, buf, 0) == pdTRUE) {
            onData(_subs[i].topic, buf);
        }
    }
}

/**
 * @brief Подписаться на топик с существующей очередью.
 * 
 * Регистрирует модуль в Router и добавляет запись в локальный массив подписок.
 * 
 * @param topic Идентификатор топика (например, Topic::SENSOR)
 * @param queue Указатель на очередь FreeRTOS
 * @param elemSize Размер одного элемента в очереди
 * @param depth Глубина очереди (1 = OVERWRITE, >1 = SEND)
 * @return true — успех, false — превышено количество подписок или нет Router
 */
bool Module::subscribe(uint16_t topic, QueueHandle_t queue, size_t elemSize, uint8_t depth) {
    if (_subCount >= MODULE_MAX_SUBS || !_router) return false;
    _router->subscribe(this, topic, queue, elemSize, depth);
    _subs[_subCount].topic = topic;
    _subs[_subCount].queue = queue;
    _subs[_subCount].depth = depth;
    _subCount++;
    return true;
}

/**
 * @brief Создать очередь и подписаться на топик.
 * 
 * Удобный метод для создания очереди типа OVERWRITE (глубина 1) и подписки.
 * Если подписка не удалась — очередь удаляется.
 * 
 * @param topic Идентификатор топика
 * @param elemSize Размер элемента данных
 * @return Указатель на очередь при успехе, nullptr при ошибке
 */
QueueHandle_t Module::subscribeNew(uint16_t topic, size_t elemSize) {
    QueueHandle_t q = xQueueCreate(1, elemSize);
    if (!q || !subscribe(topic, q, elemSize, 1)) {
        if (q) vQueueDelete(q);
        return nullptr;
    }
    return q;
}

/**
 * @brief Опубликовать данные в топик.
 * 
 * Отправляет данные через Router всем подписчикам топика.
 * 
 * @param topic Идентификатор топика
 * @param data Указатель на данные
 * @param size Размер данных в байтах
 * @return true — успех, false — не инициализирован Router
 */
bool Module::publish(uint16_t topic, const void* data, size_t size) {
    if (!_router) return false;
    _router->publish(topic, data, size);
    return true;
}