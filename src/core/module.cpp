#include "module.h"
#include "router.h"
#include "message.h"

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

bool Module::subscribe(uint16_t topic, QueueHandle_t queue, size_t elemSize, uint8_t depth) {
    if (_subCount >= MODULE_MAX_SUBS || !_router) return false;
    _router->subscribe(this, topic, queue, elemSize, depth);
    _subs[_subCount].topic = topic;
    _subs[_subCount].queue = queue;
    _subs[_subCount].depth = depth;
    _subCount++;
    return true;
}

QueueHandle_t Module::subscribeNew(uint16_t topic, size_t elemSize) {
    QueueHandle_t q = xQueueCreate(1, elemSize);
    if (!q || !subscribe(topic, q, elemSize, 1)) {
        if (q) vQueueDelete(q);
        return nullptr;
    }
    return q;
}

bool Module::publish(uint16_t topic, const void* data, size_t size) {
    if (!_router) return false;
    _router->publish(topic, data, size);
    return true;
}