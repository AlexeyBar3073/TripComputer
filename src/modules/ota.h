#ifndef OTA_H
#define OTA_H

#include "core/module.h"
#include "core/ota_chunk.h"
#include <Update.h>

class Ota : public Module {
public:
    Ota() : Module("OTA") {}
    
protected:
    bool onInit() override;
    void onProcess() override;
    void onCommand(const CommandMsg& cmd) override;
    void onData(uint16_t topic, const void* data) override;
    
private:
    QueueHandle_t chunkQueue;
    
    bool otaActive = false;
    size_t firmwareSize = 0;
    size_t written = 0;
    uint16_t chunkSize = OTA_CHUNK_BIN_SIZE;
    uint16_t totalChunks = 0;
    uint16_t expectedPack = 1;
    unsigned long lastChunkTime = 0;
    
    static constexpr unsigned long OTA_TIMEOUT_MS = 30000;
    
    uint8_t decodeBuf[OTA_DECODE_BUF_SIZE];
    
    bool initializeUpdate();
    void processChunk(const OtaChunk* chunk);
    uint16_t crc16(const uint8_t* data, size_t len);
    int base64Decode(const char* input, uint8_t* output, int maxLen);
};

#endif