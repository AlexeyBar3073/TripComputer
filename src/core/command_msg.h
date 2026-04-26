#ifndef COMMAND_MSG_H
#define COMMAND_MSG_H

#include <stdint.h>

/**
 * @brief Структура команды с одним параметром типа float.
 * 
 * Передаётся через очередь команд (TOPIC_CMD).
 * cmd  — код команды (enum Command)
 * value — параметр (например: odo_value для CMD_CORRECT_ODO, 
 *         connected=1/0 для CMD_TRANSPORT_STATUS)
 */
struct CommandMsg {
    uint8_t cmd;
    float   value;
};

#endif