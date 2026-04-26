#include <Arduino.h>
#include "transport.h"
#include "core/message.h"
#include "core/logging.h"
#include "core/command_msg.h"

bool Transport::onInit() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    
    if (!btSerial.begin("TripComputer")) {
        LOG_ERROR(name, "BT init failed");
        return false;
    }
    LOG_INFO(name, "BT started");
    
    // Подписка на исходящие сообщения — будут приходить через onData()
    subscribeNew(Topic::PROTOCOL, 512);
    
    rxLen = 0;
    memset(rxBuffer, 0, RX_BUF_SIZE);
    return true;
}

void Transport::onData(uint16_t topic, const void* data) {
    if (topic == Topic::PROTOCOL) {
        const char* msg = (const char*)data;
        btSerial.write((uint8_t*)msg, strlen(msg));
        btSerial.write('\n');
    }
}

void Transport::onProcess() {
    checkConnection();
    readBluetooth();
}

void Transport::checkConnection() {
    bool now = btSerial.hasClient();
    if (now != connected) {
        connected = now;
        digitalWrite(LED_BUILTIN, connected ? HIGH : LOW);
        LOG_INFO(name, "BT %s", connected ? "CONNECTED" : "DISCONNECTED");
        CommandMsg cmd = {CMD_TRANSPORT_STATUS, connected ? 1.0f : 0.0f};
        publish(Topic::SYSTEM, &cmd, sizeof(cmd));
    }
}

void Transport::readBluetooth() {
    if (!connected) return;
    
    while (btSerial.available() && rxLen < RX_BUF_SIZE - 1) {
        char c = btSerial.read();
        if (c == '\r') continue;
        
        rxBuffer[rxLen++] = c;
        
        if (c == '\n') {
            rxBuffer[rxLen - 1] = '\0';
            rxLen--;
            if (rxLen > 0) processLine();
            rxLen = 0;
        }
        
        if (rxLen >= RX_BUF_SIZE - 1) {
            LOG_ERROR(name, "RX overflow");
            rxLen = 0;
        }
    }
}

void Transport::processLine() {
    publish(Topic::TRANSPORT, rxBuffer, strlen(rxBuffer) + 1);
}