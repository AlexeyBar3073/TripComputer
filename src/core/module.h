#ifndef MODULE_H
#define MODULE_H

#include "command_msg.h"
#include "logging.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define MODULE_MAX_SUBS 6

class Router;

class Module {
public:
    const char* name;
    
    explicit Module(const char* moduleName)
        : name(moduleName), _router(nullptr), _cmdQueue(nullptr), _subCount(0)
    {
        memset(_subs, 0, sizeof(_subs));
    }
    
    virtual ~Module() = default;
    
    bool init(Router* router);
    void process();
    
protected:
    Router*       _router;
    QueueHandle_t _cmdQueue;
    
    // --- Хуки ---
    virtual bool onInit() { return true; }
    virtual void onProcess() {}
    virtual void onCommand(const CommandMsg& cmd) {}
    virtual void onData(uint16_t topic, const void* data) {}
    
    // --- Методы для наследников ---
    
    /** Подписаться на топик с уже созданной очередью */
    bool subscribe(uint16_t topic, QueueHandle_t queue, size_t elemSize, uint8_t depth = 1);
    
    /** Создать OVERWRITE-очередь и подписаться на топик */
    QueueHandle_t subscribeNew(uint16_t topic, size_t elemSize);
    
    /** Отправить данные в топик */
    bool publish(uint16_t topic, const void* data, size_t size);
    
private:
    struct SubEntry {
        uint16_t      topic;
        QueueHandle_t queue;
        uint8_t       depth;
    };
    SubEntry _subs[MODULE_MAX_SUBS];
    uint8_t  _subCount;
};

#endif