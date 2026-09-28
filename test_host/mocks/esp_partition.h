#pragma once

// Host mock for ESP-IDF esp_partition.h: declarations only (no flash
// operations run on host).

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_partition_t esp_partition_t;

#ifdef __cplusplus
}
#endif
