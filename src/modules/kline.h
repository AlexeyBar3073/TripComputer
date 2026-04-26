#ifndef KLINE_H
#define KLINE_H

#include "core/module.h"
#include "core/packets.h"

class KLine : public Module {
public:
    KLine() : Module("KLine") {}
    
protected:
    bool onInit() override;
    void onProcess() override;
    void onCommand(const CommandMsg& cmd) override;
    
private:
    KlinePack pack;
    unsigned long lastPublish = 0;
};

#endif