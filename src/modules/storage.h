#ifndef STORAGE_H
#define STORAGE_H

#include "core/module.h"
#include "core/packets.h"
#include <Preferences.h>

class Storage : public Module {
public:
    Storage() : Module("Storage") {}
    
protected:
    bool onInit() override;
    void onProcess() override;
    void onCommand(const CommandMsg& cmd) override;
    void onData(uint16_t topic, const void* data) override;
    
private:
    Preferences prefs;
    TripPack     savedTrip;
    SettingsPack savedSettings;
    bool tripDirty = false, settingsDirty = false;
    unsigned long lastTripSave = 0, lastSettingsSave = 0;
    
    void loadAndPublish();
    void saveTrip();
    void saveSettings();
    void handleGetCfg(int msgId);
    void handleSetCfg(const char* json);
};

#endif