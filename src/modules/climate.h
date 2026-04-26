#ifndef CLIMATE_H
#define CLIMATE_H

#include "core/module.h"
#include "core/packets.h"

class Climate : public Module {
public:
    Climate() : Module("Climate") {}
    
protected:
    bool onInit() override;
    void onProcess() override;
    
private:
    ClimatePack pack;
    unsigned long lastPublish = 0;
};

#endif