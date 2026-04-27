/**
 * @file storage.h
 * @brief Модуль сохранения и загрузки данных во Flash-память.
 * 
 * Класс Storage использует ESP-IDF Preferences API для сохранения
 * критически важных данных между перезагрузками:
 * - Текущие значения пробега и расхода (TripPack)
 * - Настройки устройства (SettingsPack)
 * 
 * Также обрабатывает команды get_cfg и set_cfg, преобразуя
 * данные между JSON и бинарным форматом.
 */

#ifndef STORAGE_H
#define STORAGE_H

#include "core/module.h"
#include "core/packets.h"
#include <Preferences.h>

/**
 * @class Storage
 * @brief Модуль хранения данных во Flash-памяти.
 * 
 * Отвечает за:
 * - Сохранение и загрузку TripPack и SettingsPack
 * - Обработку команд get_cfg / set_cfg
 * - Автоматическое сохранение при изменениях
 * - Поддержку JSON-интерфейса для настроек
 */
class Storage : public Module {
public:
    /**
     * @brief Конструктор модуля.
     * 
     * Устанавливает имя "Storage" для логирования.
     */
    Storage() : Module("Storage") {}

protected:
    /**
     * @brief Инициализация модуля.
     * 
     * Подписывается на Topic::CALCULATOR для сохранения TripPack.
     * Загружает все сохранённые данные и публикует их.
     * 
     * @return true при успехе, false при ошибке
     */
    bool onInit() override;

    /**
     * @brief Периодическая обработка.
     * 
     * Каждые 2 секунды после изменения сохраняет настройки в Flash.
     * Обеспечивает надёжность при частых изменениях.
     */
    void onProcess() override;

    /**
     * @brief Обработка команд.
     * 
     * Обрабатывает CMD_GET_CFG — запрос настроек.
     * 
     * @param cmd Команда от системы
     */
    void onCommand(const CommandMsg& cmd) override;

    /**
     * @brief Обработка входящих данных.
     * 
     * При изменении TripPack сохраняет его в Flash.
     * 
     * @param topic Идентификатор топика
     * @param data  Указатель на данные
     */
    void onData(uint16_t topic, const void* data) override;

private:
    /**
     * @brief Интерфейс к Flash-памяти.
     * 
     * Используется для сохранения и загрузки данных.
     */
    Preferences prefs;

    /**
     * @brief Последнее сохранённое состояние поездки.
     */
    TripPack     savedTrip;

    /**
     * @brief Последние сохранённые настройки.
     */
    SettingsPack savedSettings;

    /**
     * @brief Флаги изменений данных.
     * 
     * tripDirty — TripPack изменён и требует сохранения
     * settingsDirty — настройки изменены и требуют сохранения
     */
    bool tripDirty = false, settingsDirty = false;

    /**
     * @brief Время последней попытки сохранения.
     * 
     * lastTripSave — для TripPack
     * lastSettingsSave — для настроек
     * Используется для дебаунсинга и предотвращения частых записей
     */
    unsigned long lastTripSave = 0, lastSettingsSave = 0;

    /**
     * @brief Загрузка данных и публикация.
     * 
     * Загружает TripPack и SettingsPack из Flash и публикует их
     * в соответствующие топики для других модулей.
     */
    void loadAndPublish();

    /**
     * @brief Сохранение данных поездки.
     * 
     * Записывает savedTrip в раздел "bkc_v2" под ключом "trip".
     */
    void saveTrip();

    /**
     * @brief Сохранение настроек.
     * 
     * Записывает savedSettings в раздел "bkc_v2" под ключом "settings".
     */
    void saveSettings();

    /**
     * @brief Обработка запроса настроек.
     * 
     * Формирует JSON-ответ с текущими настройками.
     * 
     * @param msgId Идентификатор сообщения для подтверждения
     */
    void handleGetCfg(int msgId);

    /**
     * @brief Обработка установки настроек.
     * 
     * Разбирает JSON и обновляет savedSettings.
     * 
     * @param json Указатель на JSON-строку с настройками
     */
    void handleSetCfg(const char* json);
};

#endif