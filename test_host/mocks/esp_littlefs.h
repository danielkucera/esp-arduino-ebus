#pragma once

// Minimal esp_littlefs stub for host testing

#ifdef __cplusplus
extern "C" {
#endif

#include <cstdint>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x101
#define ESP_ERR_INVALID_STATE 0x102

typedef struct {
  const char* base_path;
  const char* partition_label;
  void* partition;
  bool format_if_mount_failed;
  bool read_only;
  bool dont_mount;
  bool grow_on_mount;
} esp_vfs_littlefs_conf_t;

inline esp_err_t esp_vfs_littlefs_register(
    const esp_vfs_littlefs_conf_t* conf) {
  return ESP_OK;
}
inline esp_err_t esp_vfs_littlefs_unregister(const char* base_path) {
  return ESP_ERR_NOT_FOUND;
}

#ifdef __cplusplus
}
#endif
