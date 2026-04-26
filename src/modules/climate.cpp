#include "climate.h"
#include "core/message.h"
#include "core/logging.h"

bool Climate::onInit() {
    memset(&pack, 0, sizeof(pack));
    pack.version = 1;
    pack.interior_temp = 22; pack.exterior_temp = 15;
    return true;
}

void Climate::onProcess() {
    unsigned long now = millis();
    if (now - lastPublish >= 1000) {
        lastPublish = now;
        pack.interior_temp = 20 + random(0,50)/10.0f;
        pack.exterior_temp = 10 + random(0,80)/10.0f;
        pack.tire_pressure = random(0,100) < 5;
        pack.washer_level = random(0,100) < 3;
        publish(Topic::SERVICE, &pack, sizeof(pack));
    }
}