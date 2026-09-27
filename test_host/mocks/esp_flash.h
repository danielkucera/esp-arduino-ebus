#pragma once

#include "esp_system.h"

typedef struct esp_flash_t {
  int unused;
} esp_flash_t;

inline esp_flash_t host_flash_chip{};
inline esp_flash_t* esp_flash_default_chip = &host_flash_chip;

inline esp_err_t esp_flash_get_size(const esp_flash_t*, uint32_t* size) {
  if (size == nullptr) return ESP_FAIL;
  *size = 4 * 1024 * 1024;
  return ESP_OK;
}
