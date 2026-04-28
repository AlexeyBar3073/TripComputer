/**
 * @file ota.cpp
 * @brief Модуль OTA-обновления прошивки.
 * 
 * Принимает команды от Protocol, получает чанки прошивки через Topic::OTA,
 * декодирует base64, проверяет CRC16, пишет во flash через Update.h.
 * После успешной записи всех чанков — отправляет ota_restart и перезагружается.
 */

#include "ota.h"
#include "core/message.h"
#include "core/logging.h"
#include <Update.h>

/** @brief Таблица символов Base64 для декодирования */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool Ota::onInit() {
    chunkQueue = xQueueCreate(1, sizeof(OtaChunk));
    if (!chunkQueue) return false;
    
    subscribe(Topic::OTA, chunkQueue, sizeof(OtaChunk), 1);  // OVERWRITE — андроид ждёт подтверждения
    return true;
}

void Ota::onCommand(const CommandMsg& cmd) {
    LOG_INFO(name, "Got cmd: 0x%02X, value=%.0f", cmd.cmd, cmd.value);
    
    switch (cmd.cmd) {
        /**
         * @brief CMD_OTA_UPDATE — начало OTA-обновления.
         * 
         * Получает размер прошивки от Protocol, рассчитывает количество чанков,
         * отправляет CMD_OTA_INIT с размером одного чанка.
         */
        case CMD_OTA_UPDATE:
            if (!otaActive) {
                firmwareSize = (size_t)cmd.value;
                totalChunks = (firmwareSize + chunkSize - 1) / chunkSize;
                expectedPack = 1;
                written = 0;
                lastChunkTime = millis();
                
                LOG_INFO(name, "OTA update: size=%u, chunks=%d", firmwareSize, totalChunks);
                
                CommandMsg resp = {CMD_OTA_INIT, (float)chunkSize};
                publish(Topic::SYSTEM, &resp, sizeof(resp));
            }
            break;

        /**
         * @brief CMD_OTA_END — завершение OTA.
         * 
         * Вызывает Update.end() для проверки целостности прошивки.
         * При успехе отправляет ota_restart и перезагружается.
         */
        case CMD_OTA_END:
            if (otaActive && Update.end(true)) {
                // Шлём ota_restart клиенту перед перезагрузкой
                CommandMsg resp = {CMD_OTA_RESTART, 0};
                publish(Topic::SYSTEM, &resp, sizeof(resp));

                LOG_INFO(name, "OTA complete: %u bytes, rebooting...", written);
                vTaskDelay(pdMS_TO_TICKS(200));  // даём время на отправку ota_restart
                ESP.restart();
            } else {
                LOG_ERROR(name, "OTA end failed");
            }
            break;
    }
}

/**
 * @brief Получение чанка прошивки из Topic::OTA.
 * 
 * Копирует данные из общего буфера в локальную переменную (чтобы избежать
 * перезаписи при переключении задач), затем обрабатывает чанк.
 */
void Ota::onData(uint16_t topic, const void* data) {
    if (topic == Topic::OTA) {
        OtaChunk chunk;
        memcpy(&chunk, data, sizeof(OtaChunk));
        
        LOG_INFO(name, "Got chunk: pack=%d, len=%d", chunk.pack, chunk.bin_len);
        
        if (otaActive) {
            processChunk(&chunk);
        } else {
            LOG_INFO(name, "OTA not active, initializing...");
            if (initializeUpdate()) {
                processChunk(&chunk);
            }
        }
    }
}

/**
 * @brief Проверка таймаута OTA.
 * 
 * Если чанки не приходят более 30 секунд — прерывает обновление.
 */
void Ota::onProcess() {
    if (otaActive && (millis() - lastChunkTime > OTA_TIMEOUT_MS)) {
        LOG_ERROR(name, "OTA timeout, aborting");
        Update.abort();
        otaActive = false;
    }
}

/**
 * @brief Инициализация Update.begin() при получении первого чанка.
 * 
 * Проверяет, что размер прошивки не превышает доступное место.
 * Вызывается лениво — не при получении команды, а при первом чанке.
 * Это позволяет другим задачам завершиться и освободить память.
 */
bool Ota::initializeUpdate() {
    if (otaActive) return true;
    
    if (firmwareSize > ESP.getFreeSketchSpace()) {
        LOG_ERROR(name, "Firmware too large: %u > %u", firmwareSize, ESP.getFreeSketchSpace());
        return false;
    }
    
    if (!Update.begin(firmwareSize)) {
        LOG_ERROR(name, "Update.begin failed");
        return false;
    }
    
    otaActive = true;
    LOG_INFO(name, "Update initialized, ready for %d chunks", totalChunks);
    return true;
}

/**
 * @brief Обработка одного чанка прошивки.
 * 
 * Порядок:
 * 1. Проверка номера чанка (последовательность)
 * 2. Декодирование base64 в бинарные данные
 * 3. Проверка CRC16 (если передан)
 * 4. Запись во flash через Update.write()
 * 5. Отправка CMD_OTA_WRITE с номером записанного чанка (или ошибкой)
 */
void Ota::processChunk(const OtaChunk* chunk) {
    LOG_INFO(name, "Processing chunk %d", chunk->pack);
    if (!chunk || chunk->pack == 0) return;
    
    // Проверка последовательности
    if (chunk->pack != expectedPack) {
        LOG_ERROR(name, "Wrong pack: %d != %d", chunk->pack, expectedPack);
        CommandMsg resp = {CMD_OTA_WRITE, -(float)expectedPack};
        publish(Topic::SYSTEM, &resp, sizeof(resp));
        return;
    }
    
    // Декодирование base64
    int decodedLen = base64Decode(chunk->b64, decodeBuf, OTA_DECODE_BUF_SIZE);
    if (decodedLen <= 0 || decodedLen > OTA_CHUNK_BIN_SIZE) {
        LOG_ERROR(name, "Base64 decode failed: pack=%d", chunk->pack);
        CommandMsg resp = {CMD_OTA_WRITE, -(float)chunk->pack};
        publish(Topic::SYSTEM, &resp, sizeof(resp));
        return;
    }
    
    // Проверка CRC16
    if (chunk->crc16 != 0) {
        uint16_t calc = crc16(decodeBuf, decodedLen);
        if (calc != chunk->crc16) {
            LOG_ERROR(name, "CRC mismatch: pack=%d, calc=%04X, got=%04X", chunk->pack, calc, chunk->crc16);
            CommandMsg resp = {CMD_OTA_WRITE, -(float)chunk->pack};
            publish(Topic::SYSTEM, &resp, sizeof(resp));
            return;
        }
    }
    
    // Запись во flash
    size_t w = Update.write(decodeBuf, decodedLen);
    if (w != (size_t)decodedLen) {
        LOG_ERROR(name, "Write failed: pack=%d", chunk->pack);
        CommandMsg resp = {CMD_OTA_WRITE, -(float)chunk->pack};
        publish(Topic::SYSTEM, &resp, sizeof(resp));
        return;
    }
    
    written += w;
    expectedPack++;
    lastChunkTime = millis();
    
    // Подтверждение успешной записи
    CommandMsg resp = {CMD_OTA_WRITE, (float)chunk->pack};
    publish(Topic::SYSTEM, &resp, sizeof(resp));
}

/**
 * @brief Вычисление CRC16 (полином 0xA001, CRC-16-IBM).
 * @param data Указатель на данные
 * @param len  Длина данных в байтах
 * @return 16-битная контрольная сумма
 */
uint16_t Ota::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

/**
 * @brief Декодирование Base64 в бинарные данные.
 * @param input  Входная строка в кодировке Base64
 * @param output Буфер для бинарных данных
 * @param maxLen Максимальный размер выходного буфера
 * @return Количество декодированных байт, или -1 при переполнении
 */
int Ota::base64Decode(const char* input, uint8_t* output, int maxLen) {
    int outLen = 0;
    int val = 0, valb = -8;
    
    for (; *input; input++) {
        // Пропускаем пробелы, переводы строк и символы заполнения
        if (*input == '=' || *input == '\r' || *input == '\n' || *input == ' ') continue;
        
        const char* p = strchr(B64, *input);
        if (!p) continue;
        
        val = (val << 6) + (p - B64);
        valb += 6;
        if (valb >= 0) {
            if (outLen >= maxLen) return -1;
            output[outLen++] = (val >> valb) & 0xFF;
            valb -= 8;
        }
    }
    return outLen;
}