#pragma once

// Minimal esp_log stub for host testing

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LOG_NONE 0
#define ESP_LOG_ERROR 1
#define ESP_LOG_WARN 2
#define ESP_LOG_INFO 3
#define ESP_LOG_DEBUG 4
#define ESP_LOG_VERBOSE 5

#define ESP_LOG_TAG "host"

inline void esp_log_write(int level, const char* tag, const char* format, ...) {
}

#define ESP_LOG_BUFFER_HEXDUMP(tag, buf, buf_len, level) \
  do {                                                   \
  } while (0)

#ifdef __cplusplus
}
#endif
