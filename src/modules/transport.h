#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "core/module.h"
#include <BluetoothSerial.h>

class Transport : public Module {
public:
    Transport() : Module("Transport"), connected(false), rxLen(0) {}
    
protected:
    bool onInit() override;
    void onProcess() override;
    void onData(uint16_t topic, const void* data) override;
    
private:
    BluetoothSerial btSerial;
    bool connected;
    
    static constexpr size_t RX_BUF_SIZE = 512;
    char rxBuffer[RX_BUF_SIZE];
    uint16_t rxLen;
    
    void checkConnection();
    void readBluetooth();
    void processLine();
};

#endif