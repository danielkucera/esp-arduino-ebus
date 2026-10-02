#pragma once

// Host mock for ESP-IDF esp_ota_ops.h: declarations only (no OTA runs
// on host).

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t esp_ota_handle_t;

#ifdef __cplusplus
}
#endif
