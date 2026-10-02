#pragma once

// Host mock for ESP-IDF esp_err.h (decouples NVS mock from real IDF).

#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_ERR_NVS_BASE 0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_READ_ONLY (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_NAME (ESP_ERR_NVS_BASE + 0x06)
#define ESP_ERR_NVS_INVALID_HANDLE (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_REMOVE_FAILED (ESP_ERR_NVS_BASE + 0x08)
#define ESP_ERR_NVS_KEY_TOO_LONG (ESP_ERR_NVS_BASE + 0x09)
#define ESP_ERR_NVS_PAGE_FULL (ESP_ERR_NVS_BASE + 0x0A)
#define ESP_ERR_NVS_INVALID_STATE (ESP_ERR_NVS_BASE + 0x0B)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x0C)
#define ESP_ERR_NVS_NO_FREE_PAGES (ESP_ERR_NVS_BASE + 0x0D)
#define ESP_ERR_NVS_VALUE_TOO_LONG (ESP_ERR_NVS_BASE + 0x0E)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)

inline const char* esp_err_to_name(esp_err_t code) {
  return code == ESP_OK ? "ESP_OK" : "MOCK_ESP_ERR";
}

#ifdef __cplusplus
}
#endif
