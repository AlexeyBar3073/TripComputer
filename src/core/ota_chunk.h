#ifndef OTA_CHUNK_H
#define OTA_CHUNK_H

#include <stdint.h>

#define OTA_CHUNK_BIN_SIZE  320   // бинарных данных в чанке
#define OTA_CHUNK_B64_SIZE  428   // base64 от 320 байт
#define OTA_DECODE_BUF_SIZE 512

struct OtaChunk {
    uint16_t pack;      // номер чанка (1..N)
    uint16_t crc16;     // CRC16 бинарных данных
    uint16_t bin_len;   // длина бинарных данных
    char     b64[OTA_CHUNK_B64_SIZE + 1];  // base64 строка
};

#endif