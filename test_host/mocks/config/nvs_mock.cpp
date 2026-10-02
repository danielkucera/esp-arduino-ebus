// Host mock backend for ESP-IDF NVS: single-namespace in-memory map.
// Faithfully replicates the nvs_get_str two-call protocol (length query
// including NUL, then fetch with guaranteed NUL termination).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "nvs.h"

namespace {

struct StoredValue {
  nvs_type_t type = NVS_TYPE_STR;
  std::string str;
  int64_t integer = 0;
};

std::map<std::string, StoredValue>& store() {
  static std::map<std::string, StoredValue> instance;
  return instance;
}

uint32_t next_handle = 1;

const StoredValue* find(const char* key) {
  auto it = store().find(key != nullptr ? key : "");
  return it != store().end() ? &it->second : nullptr;
}

bool typeMatches(nvs_type_t filter, nvs_type_t actual) {
  return filter == NVS_TYPE_ANY || filter == actual;
}

}  // namespace

struct NvsMockIter_ {
  std::map<std::string, StoredValue>::iterator it;
  std::map<std::string, StoredValue>::iterator end;
  nvs_type_t filter;
};

extern "C" {

esp_err_t nvs_flash_init(void) { return ESP_OK; }

esp_err_t nvs_flash_erase(void) {
  store().clear();
  return ESP_OK;
}

esp_err_t nvs_open(const char* ns_name, nvs_open_mode_t open_mode,
                   nvs_handle_t* out_handle) {
  (void)ns_name;
  (void)open_mode;
  if (out_handle == nullptr) return ESP_ERR_NVS_INVALID_HANDLE;
  *out_handle = next_handle++;
  return ESP_OK;
}

void nvs_close(nvs_handle_t handle) { (void)handle; }

esp_err_t nvs_commit(nvs_handle_t handle) {
  (void)handle;
  return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle) {
  (void)handle;
  store().clear();
  return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char* key, const char* value) {
  (void)handle;
  if (key == nullptr || value == nullptr) return ESP_ERR_NVS_INVALID_NAME;
  StoredValue v;
  v.type = NVS_TYPE_STR;
  v.str = value;
  store()[key] = v;
  return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char* key, char* out_value,
                      size_t* length) {
  (void)handle;
  const StoredValue* v = find(key);
  if (v == nullptr || v->type != NVS_TYPE_STR) {
    return ESP_ERR_NVS_NOT_FOUND;
  }
  if (out_value == nullptr) {
    if (length != nullptr) *length = v->str.size() + 1;  // incl. NUL
    return ESP_OK;
  }
  if (length == nullptr || *length == 0) {
    return ESP_ERR_NVS_INVALID_LENGTH;
  }
  size_t capacity = *length;
  size_t copy_len = v->str.size();
  if (copy_len > capacity - 1) copy_len = capacity - 1;
  std::memcpy(out_value, v->str.data(), copy_len);
  out_value[copy_len] = '\0';
  *length = copy_len + 1;
  return ESP_OK;
}

#define NVS_MOCK_INT_SET_GET(bits, ctype, nvs_enum)                        \
  esp_err_t nvs_set_##bits(nvs_handle_t handle, const char* key,           \
                           ctype value) {                                  \
    (void)handle;                                                          \
    if (key == nullptr) return ESP_ERR_NVS_INVALID_NAME;                   \
    StoredValue v;                                                         \
    v.type = NVS_TYPE_##nvs_enum;                                          \
    v.integer = static_cast<int64_t>(value);                               \
    store()[key] = v;                                                      \
    return ESP_OK;                                                         \
  }                                                                        \
  esp_err_t nvs_get_##bits(nvs_handle_t handle, const char* key,           \
                           ctype* out_value) {                             \
    (void)handle;                                                          \
    const StoredValue* v = find(key);                                      \
    if (v == nullptr || v->type != NVS_TYPE_##nvs_enum) {                  \
      return ESP_ERR_NVS_NOT_FOUND;                                        \
    }                                                                      \
    if (out_value != nullptr) *out_value = static_cast<ctype>(v->integer); \
    return ESP_OK;                                                         \
  }

NVS_MOCK_INT_SET_GET(i8, int8_t, I8)
NVS_MOCK_INT_SET_GET(u8, uint8_t, U8)
NVS_MOCK_INT_SET_GET(i16, int16_t, I16)
NVS_MOCK_INT_SET_GET(u16, uint16_t, U16)
NVS_MOCK_INT_SET_GET(i32, int32_t, I32)
NVS_MOCK_INT_SET_GET(u32, uint32_t, U32)
NVS_MOCK_INT_SET_GET(i64, int64_t, I64)
NVS_MOCK_INT_SET_GET(u64, uint64_t, U64)

esp_err_t nvs_entry_find(const char* part_name, const char* ns_name,
                         nvs_type_t type, nvs_iterator_t* out_it) {
  (void)part_name;
  (void)ns_name;
  if (out_it == nullptr) return ESP_ERR_NVS_INVALID_HANDLE;
  NvsMockIter_* it = new NvsMockIter_();
  it->it = store().begin();
  it->end = store().end();
  it->filter = type;
  while (it->it != it->end && !typeMatches(type, it->it->second.type)) {
    ++it->it;
  }
  if (it->it == it->end) {
    delete it;
    *out_it = nullptr;
    return ESP_ERR_NVS_NOT_FOUND;
  }
  *out_it = it;
  return ESP_OK;
}

esp_err_t nvs_entry_next(nvs_iterator_t* it) {
  if (it == nullptr || *it == nullptr) return ESP_ERR_NVS_NOT_FOUND;
  do {
    ++(*it)->it;
  } while ((*it)->it != (*it)->end &&
           !typeMatches((*it)->filter, (*it)->it->second.type));
  return (*it)->it != (*it)->end ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t* out_info) {
  if (it == nullptr || out_info == nullptr) return ESP_ERR_NVS_INVALID_HANDLE;
  if (it->it == it->end) return ESP_ERR_NVS_NOT_FOUND;
  out_info->type = it->it->second.type;
  std::snprintf(out_info->key, sizeof(out_info->key), "%s",
                it->it->first.c_str());
  return ESP_OK;
}

void nvs_release_iterator(nvs_iterator_t it) { delete it; }

}  // extern "C"
