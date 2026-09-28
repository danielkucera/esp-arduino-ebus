#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_system.h"

typedef enum { EFUSE_BLK3 = 3 } esp_efuse_block_t;

typedef struct {
  esp_efuse_block_t efuse_block;
  size_t bit_start;
  size_t bit_count;
} esp_efuse_desc_t;

esp_err_t esp_efuse_read_field_blob(const esp_efuse_desc_t** field, void* dst,
                                    size_t dst_size_bits);
