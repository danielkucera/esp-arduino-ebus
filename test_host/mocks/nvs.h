#pragma once

// Host mock for ESP-IDF nvs.h: in-memory key/value backend.
// See nvs_mock.cpp for the implementation.

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nvs_handle_t;

typedef enum {
  NVS_READONLY,
  NVS_READWRITE,
} nvs_open_mode_t;

typedef enum {
  NVS_TYPE_ANY = 0,
  NVS_TYPE_U8 = 1,
  NVS_TYPE_I8 = 2,
  NVS_TYPE_U16 = 3,
  NVS_TYPE_I16 = 4,
  NVS_TYPE_U32 = 5,
  NVS_TYPE_I32 = 6,
  NVS_TYPE_U64 = 7,
  NVS_TYPE_I64 = 8,
  NVS_TYPE_STR = 9,
  NVS_TYPE_BLOB = 10,
} nvs_type_t;

#define NVS_KEY_NAME_MAX_SIZE 16

typedef struct {
  nvs_type_t type;
  char key[NVS_KEY_NAME_MAX_SIZE];
} nvs_entry_info_t;

typedef struct NvsMockIter_* nvs_iterator_t;

esp_err_t nvs_open(const char* ns_name, nvs_open_mode_t open_mode,
                   nvs_handle_t* out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_all(nvs_handle_t handle);

esp_err_t nvs_set_str(nvs_handle_t handle, const char* key, const char* value);
esp_err_t nvs_get_str(nvs_handle_t handle, const char* key, char* out_value,
                      size_t* length);

esp_err_t nvs_set_i8(nvs_handle_t handle, const char* key, int8_t value);
esp_err_t nvs_get_i8(nvs_handle_t handle, const char* key, int8_t* out_value);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out_value);
esp_err_t nvs_set_i16(nvs_handle_t handle, const char* key, int16_t value);
esp_err_t nvs_get_i16(nvs_handle_t handle, const char* key, int16_t* out_value);
esp_err_t nvs_set_u16(nvs_handle_t handle, const char* key, uint16_t value);
esp_err_t nvs_get_u16(nvs_handle_t handle, const char* key,
                      uint16_t* out_value);
esp_err_t nvs_set_i32(nvs_handle_t handle, const char* key, int32_t value);
esp_err_t nvs_get_i32(nvs_handle_t handle, const char* key, int32_t* out_value);
esp_err_t nvs_set_u32(nvs_handle_t handle, const char* key, uint32_t value);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char* key,
                      uint32_t* out_value);
esp_err_t nvs_set_i64(nvs_handle_t handle, const char* key, int64_t value);
esp_err_t nvs_get_i64(nvs_handle_t handle, const char* key, int64_t* out_value);
esp_err_t nvs_set_u64(nvs_handle_t handle, const char* key, uint64_t value);
esp_err_t nvs_get_u64(nvs_handle_t handle, const char* key,
                      uint64_t* out_value);

esp_err_t nvs_entry_find(const char* part_name, const char* ns_name,
                         nvs_type_t type, nvs_iterator_t* out_it);
esp_err_t nvs_entry_next(nvs_iterator_t* it);
esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t* out_info);
void nvs_release_iterator(nvs_iterator_t it);

#ifdef __cplusplus
}
#endif
