#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>

// #define DEBUG_ENABLE

#define LOG_BUF_SIZE 128

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_DEBUG 3

#ifndef LOG_LEVEL
  #ifdef DEBUG_ENABLE
    #define LOG_LEVEL LOG_LEVEL_DEBUG
  #else
    #define LOG_LEVEL LOG_LEVEL_NONE
  #endif
#endif

#ifdef DEBUG_ENABLE
  #define LOG_PRINT(level, tag, fmt, ...) do {                                \
    if (LOG_LEVEL >= level) {                                                 \
      static char log_buf[LOG_BUF_SIZE];                                      \
      const char* level_str = (level == LOG_LEVEL_ERROR) ? "E" :              \
                              (level == LOG_LEVEL_INFO)  ? "I" : "D";         \
      int prefix_len = snprintf(log_buf, LOG_BUF_SIZE, "[%lu][%s][%s] ",      \
                                millis(), level_str, tag);                    \
      if (prefix_len < LOG_BUF_SIZE - 1) {                                    \
        snprintf(log_buf + prefix_len, LOG_BUF_SIZE - prefix_len,             \
                 fmt, ##__VA_ARGS__);                                         \
      }                                                                       \
      Serial.println(log_buf);                                                \
    }                                                                         \
  } while(0)
#else
  #define LOG_PRINT(level, tag, fmt, ...) ((void)0)
#endif

#define LOG_ERROR(tag, fmt, ...) LOG_PRINT(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  LOG_PRINT(LOG_LEVEL_INFO,  tag, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...) LOG_PRINT(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)

#ifdef DEBUG_ENABLE
  #define LOG_ASSERT(cond, tag, msg) do {                                     \
    if (!(cond)) {                                                            \
      LOG_ERROR(tag, "ASSERT: %s", msg);                                      \
    }                                                                         \
  } while(0)
#else
  #define LOG_ASSERT(cond, tag, msg) ((void)0)
#endif

#endif