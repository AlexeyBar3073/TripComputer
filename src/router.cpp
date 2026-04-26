#include "router.h"
#include "core/logging.h"

void Router::init() {
    memset(_topics, 0, sizeof(_topics));
    LOG_INFO("ROUTER", "Init done");
}

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
    
    if (te.retainValid) {
        if (depth == 1)
            xQueueOverwrite(queue, te.retainData);
        else
            xQueueSend(queue, te.retainData, 0);
    }
}

void Router::publish(uint16_t topic, const void* data, size_t size) {
    int idx = topicIndex(topic);
    if (idx < 0 || idx >= 16) return;
    
    TopicEntry& te = _topics[idx];
    
    if (size <= ROUTER_RETAIN_SIZE) {
        memcpy(te.retainData, data, size);
        te.retainSize = size;
        te.retainValid = true;
    }
    
    for (uint8_t i = 0; i < te.count; i++) {
        if (!te.subs[i].queue) continue;
        if (te.subs[i].depth == 1)
            xQueueOverwrite(te.subs[i].queue, data);
        else
            xQueueSend(te.subs[i].queue, data, 0);
    }
}

int Router::topicIndex(uint16_t topic) {
    for (int i = 0; i < 16; i++) {
        if (topic & (1 << i)) return i;
    }
    return -1;
}