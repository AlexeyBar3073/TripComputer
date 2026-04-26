#include <Arduino.h>
#include "router.h"
#include "modules/transport.h"
#include "modules/protocol.h"
#include "modules/simulator.h"
#include "modules/calculator.h"
#include "modules/storage.h"
#include "modules/kline.h"
#include "modules/climate.h"

Router     router;
Transport  transport;
Protocol   protocol;
Simulator  simulator;
Calculator calculator;
Storage    storage;
KLine      kline;
Climate    climate;

void transportProtocolTask(void*) {
    while (1) {
        transport.process();
        protocol.process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void engineTask(void*) {
    while (1) {
        simulator.process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void klineTask(void*) {
    while (1) {
        kline.process();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void mainTask(void*) {
    while (1) {
        calculator.process();
        storage.process();
        climate.process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    Serial.begin(115200);
    delay(5000);
    
    router.init();
    
    storage.init(&router);
    calculator.init(&router);
    simulator.init(&router);
    kline.init(&router);
    climate.init(&router);
    protocol.init(&router);
    transport.init(&router);
    
    xTaskCreatePinnedToCore(transportProtocolTask, "BtRx",   4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(engineTask,             "Engine", 3072, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(klineTask,              "KLine",  3072, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(mainTask,               "Main",   4096, NULL, 2, NULL, 0);
}

void loop() { delay(1000); }