#include "kline.h"
#include "core/message.h"
#include "core/logging.h"

bool KLine::onInit() {
    memset(&pack, 0, sizeof(pack));
    pack.version = 2;
    pack.coolant_temp = 90; pack.atf_temp = 75;
    pack.selector_position = 3; pack.current_gear = 2;
    strcpy(pack.dtc_codes, "P0135;P0141");
    return true;
}

void KLine::onCommand(const CommandMsg& cmd) {
    switch (cmd.cmd) {
        case CMD_KL_CLEAR_DTC: memset(pack.dtc_codes, 0, 64); break;
        case CMD_KL_DETECT_PROTO: LOG_INFO(name, "Proto detect"); break;
    }
}

void KLine::onProcess() {
    unsigned long now = millis();
    if (now - lastPublish >= 1000) {
        lastPublish = now;
        pack.coolant_temp = 85 + random(0,20)/10.0f;
        pack.atf_temp = 70 + random(0,15)/10.0f;
        pack.voltage = 14 + random(0,50)/100.0f;
        pack.fuel_percent = 55 + random(0,20)/10.0f;
        pack.output_shaft_rpm = 1500 + random(0,500);
        publish(Topic::KLINE, &pack, sizeof(pack));
    }
}