#ifndef ROUTER_H
#define ROUTER_H

#include "core/module.h"
#include "core/command_msg.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define ROUTER_MAX_SUBS 6
#define ROUTER_RETAIN_SIZE 128

class Router {
public:
    void init();
    
    // Подписка модуля на топик
    void subscribe(Module* module, uint16_t topic, QueueHandle_t queue,
                   size_t elemSize, uint8_t depth);
    
    // Публикация данных в топик — синхронная, сразу в очереди подписчиков + retain-кэш
    void publish(uint16_t topic, const void* data, size_t size);
    
private:
    struct SubSlot {
        QueueHandle_t queue;
        uint8_t       depth;
        size_t        elemSize;
    };
    
    struct TopicEntry {
        SubSlot subs[ROUTER_MAX_SUBS];
        uint8_t count;
        uint8_t retainData[ROUTER_RETAIN_SIZE];
        size_t  retainSize;
        bool    retainValid;
    };
    
    TopicEntry _topics[16];  // индекс = номер бита (0..15)
    
    static int topicIndex(uint16_t topic);
};

#endif