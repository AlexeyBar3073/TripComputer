/**
 * @file storage.h
 * @brief Модуль сохранения и загрузки данных во Flash-память.
 * 
 * Storage использует Preferences API для хранения критически важных данных
 * между перезагрузками: TripPack (пробег, расход) и SettingsPack (настройки).
 * 
 * Настройки хранятся в JSON для устойчивости к изменению структуры.
 * TripPack — бинарно (структура стабильна).
 */

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
    
    TripPack     savedTrip;         ///< Последнее сохранённое состояние поездки
    SettingsPack savedSettings;     ///< Последние сохранённые настройки
    bool tripDirty = false;         ///< TripPack изменён и требует сохранения
    bool settingsDirty = false;     ///< Настройки изменены и требуют сохранения
    unsigned long lastSettingsSave = 0;  ///< Время последнего сохранения настроек

    void loadAndPublish();           ///< Загрузка из NVS и публикация для модулей
    void saveTrip();                 ///< Сохранение TripPack бинарно
    void saveSettings();             ///< Сохранение SettingsPack как JSON
    void handleGetCfg(int msgId);    ///< Формирование JSON-ответа с настройками
    void handleSetCfg(const char* json);  ///< Парсинг JSON и обновление настроек
};

#endif