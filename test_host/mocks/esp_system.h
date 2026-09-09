#pragma once

// Minimal ESP-IDF esp_system types for host testing

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

typedef uint8_t uint8_t;
typedef uint16_t uint16_t;
typedef uint32_t uint32_t;
typedef uint64_t uint64_t;

// esp_chip_info_t stub
typedef struct {
  uint32_t revision;
  uint32_t features;
} esp_chip_info_t;

inline void esp_chip_info(esp_chip_info_t* info) {
  info->revision = 1;
  info->features = 0;
}

inline void esp_flash_get_size(const void* chip, uint32_t* size) {
  *size = 4 * 1024 * 1024;
}

inline const char* esp_get_idf_version(void) { return "host-test"; }
inline void esp_read_mac(uint8_t* mac, int type) {
  for (int i = 0; i < 6; i++) mac[i] = 0x12;
}
inline uint32_t esp_clk_cpu_freq(void) { return 240000000; }
inline uint32_t esp_clk_apb_freq(void) { return 240000000; }

#ifdef __cplusplus
}
#endif
