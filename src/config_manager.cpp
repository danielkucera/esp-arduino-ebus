#include "config_manager.hpp"

#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstdio>
#include <cstdlib>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>
#include <string>
#include <vector>

#include "http.hpp"
#include "http_utils.hpp"

extern ConfigManager configManager;

namespace {

constexpr const char* nvs_namespace = "esp-ebus";

bool ensureNvsReady() {
  static bool nvsReady = false;
  if (nvsReady) return true;

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) return false;

  nvsReady = true;
  return true;
}

// Static buffer for NVS string reads - avoids heap allocation
// NVS keys are typically config values (SSID, hostnames, etc.) under 128 bytes
// But use 512 to be safe for larger values like certificates or JSON configs
static char nvs_string_buffer[512];

std::string_view readString(nvs_handle_t handle, const char* key,
                            const char* fallback = "") {
  size_t required = 0;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &required);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Fallback is a string literal, safe to return as string_view
    return fallback;
  }
  if (err != ESP_OK || required == 0) {
    return fallback;
  }

  // Ensure buffer is large enough (required includes null terminator)
  if (required > sizeof(nvs_string_buffer)) {
    // Not enough space - truncate to max that fits
    required = sizeof(nvs_string_buffer) - 1;
  }

  err = nvs_get_str(handle, key, nvs_string_buffer, &required);
  if (err != ESP_OK) {
    return fallback;
  }

  // nvs_get_str always null-terminates, so we can use the simple constructor
  // which will stop at the null terminator. This also handles empty strings.
  return std::string_view(nvs_string_buffer);
}

bool writeString(nvs_handle_t handle, const char* key, const std::string& value,
                 std::string& error) {
  const esp_err_t err = nvs_set_str(handle, key, value.c_str());
  if (err != ESP_OK) {
    char err_buf[128];
    snprintf(err_buf, sizeof(err_buf), "Failed to write key '%s': %s", key,
             esp_err_to_name(err));
    error = err_buf;
    return false;
  }
  return true;
}

bool parseStoredBool(std::string_view value) {
  return value == "selected" || value == "true" || value == "1" ||
         value == "on";
}

bool readEntryValueAsString(nvs_handle_t handle, const nvs_entry_info_t& info,
                            std::string& out) {
  switch (info.type) {
    case NVS_TYPE_STR: {
      out = readString(handle, info.key);
      return true;
    }
    case NVS_TYPE_I8: {
      int8_t value = 0;
      if (nvs_get_i8(handle, info.key, &value) != ESP_OK) return false;
      out = std::to_string(value);
      return true;
    }
    case NVS_TYPE_U8: {
      uint8_t value = 0;
      if (nvs_get_u8(handle, info.key, &value) != ESP_OK) return false;
      out = std::to_string(value);
      return true;
    }
    case NVS_TYPE_I16: {
      int16_t value = 0;
      if (nvs_get_i16(handle, info.key, &value) != ESP_OK) return false;
      out = std::to_string(value);
      return true;
    }
    case NVS_TYPE_U16: {
      uint16_t value = 0;
      if (nvs_get_u16(handle, info.key, &value) != ESP_OK) return false;
      out = std::to_string(value);
      return true;
    }
    case NVS_TYPE_I32: {
      int32_t value = 0;
      if (nvs_get_i32(handle, info.key, &value) != ESP_OK) return false;
      out = std::to_string(value);
      return true;
    }
    case NVS_TYPE_U32: {
      uint32_t value = 0;
      if (nvs_get_u32(handle, info.key, &value) != ESP_OK) return false;
      out = std::to_string(value);
      return true;
    }
    case NVS_TYPE_I64: {
      int64_t value = 0;
      if (nvs_get_i64(handle, info.key, &value) != ESP_OK) return false;
      out = std::to_string(static_cast<long long>(value));
      return true;
    }
    case NVS_TYPE_U64: {
      uint64_t value = 0;
      if (nvs_get_u64(handle, info.key, &value) != ESP_OK) return false;
      out = std::to_string(static_cast<unsigned long long>(value));
      return true;
    }
    default:
      return false;
  }
}

void fillJsonFromNvs(ebus::detail::JsonWriter& writer, nvs_handle_t handle) {
  nvs_iterator_t it = nullptr;
  if (nvs_entry_find("nvs", nvs_namespace, NVS_TYPE_ANY, &it) != ESP_OK) {
    return;
  }
  while (it != nullptr) {
    nvs_entry_info_t info{};
    nvs_entry_info(it, &info);

    // Skip sensitive configuration keys in JSON output to prevent leaking
    // credentials if (std::strcmp(info.key, "wifiPassword") != 0 &&
    //     std::strcmp(info.key, "mqttPass") != 0 &&
    //     std::strcmp(info.key, "apModePassword") != 0) {
    std::string value;
    if (readEntryValueAsString(handle, info, value)) {
      writer.writeField(info.key, value);
    }
    // }

    if (nvs_entry_next(&it) != ESP_OK) {
      break;
    }
  }
  nvs_release_iterator(it);
}

}  // namespace

std::string_view ConfigManager::readString(const char* key,
                                           const char* fallback) {
  if (!ensureNvsReady()) return fallback;

  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(nvs_namespace, NVS_READONLY, &handle);
  if (openErr != ESP_OK) return fallback;

  std::string_view value = ::readString(handle, key, fallback);
  nvs_close(handle);
  return value;
}

int32_t ConfigManager::readInt(const char* key, int32_t fallback) {
  if (!ensureNvsReady()) return fallback;

  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(nvs_namespace, NVS_READONLY, &handle);
  if (openErr != ESP_OK) return fallback;

  int32_t value = fallback;
  esp_err_t err = nvs_get_i32(handle, key, &value);
  if (err == ESP_OK) {
    nvs_close(handle);
    return value;
  }

  // Backward compatibility for values stored as strings.
  std::string_view strValue = ::readString(handle, key);
  nvs_close(handle);
  if (strValue.empty()) return fallback;

  char* end = nullptr;
  const long parsed = std::strtol(strValue.data(), &end, 10);
  if (end == strValue.data() || *end != '\0') return fallback;
  return static_cast<int32_t>(parsed);
}

bool ConfigManager::readBool(const char* key, bool fallback) {
  return parseStoredBool(readString(key, fallback ? "selected" : ""));
}

bool ConfigManager::writeString(const char* key, const std::string& value) {
  if (!ensureNvsReady()) return false;

  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(nvs_namespace, NVS_READWRITE, &handle);
  if (openErr != ESP_OK) return false;

  std::string error;
  const bool ok = ::writeString(handle, key, value, error);
  if (!ok) {
    nvs_close(handle);
    return false;
  }

  const esp_err_t commitErr = nvs_commit(handle);
  nvs_close(handle);
  return commitErr == ESP_OK;
}

void ConfigManager::resetConfig() {
  if (!ensureNvsReady()) return;

  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(nvs_namespace, NVS_READWRITE, &handle);
  if (openErr != ESP_OK) return;

  const esp_err_t eraseErr = nvs_erase_all(handle);
  if (eraseErr == ESP_OK) {
    nvs_commit(handle);
  }

  nvs_close(handle);
}

namespace {
esp_err_t handleConfigGet(httpd_req_t* req) {
  return configManager.handleGet(req);
}

esp_err_t handleConfigSet(httpd_req_t* req) {
  return configManager.handleSet(req);
}

esp_err_t handleConfigReset(httpd_req_t* req) {
  return configManager.handleReset(req);
}
}  // namespace

void ConfigManager::begin() {
  ensureNvsReady();

  RegisterUri("/api/v1/config", HTTP_GET, handleConfigGet);
  RegisterUri("/api/v1/config", HTTP_POST, handleConfigSet);
  RegisterUri("/api/v1/config/reset", HTTP_POST, handleConfigReset);
}

void ConfigManager::fetchConfig(const ebus::JsonChunkVisitor& visitor) {
  if (!ensureNvsReady()) {
    visitor("{}");
    return;
  }

  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(nvs_namespace, NVS_READONLY, &handle);
  if (openErr != ESP_OK) {
    visitor("{}");
    return;
  }

  ebus::detail::JsonWriter writer(visitor);
  {
    auto root = writer.objectScope();
    auto config = writer.objectScope("config");
    fillJsonFromNvs(writer, handle);
  }
  writer.flush();
  nvs_close(handle);
}

bool ConfigManager::writeConfigJson(std::string_view body, std::string& error) {
  if (!ensureNvsReady()) {
    error = "Failed to initialize NVS";
    return false;
  }

  ebus::detail::JsonReader reader(body);
  if (reader.next() != ebus::detail::JsonReader::Token::object_start) {
    error = "JSON root must be an object";
    return false;
  }

  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(nvs_namespace, NVS_READWRITE, &handle);
  if (openErr != ESP_OK) {
    error = std::string("Failed to open NVS: ") + esp_err_to_name(openErr);
    return false;
  }

  bool dirty = false;
  bool ok = true;

  while (ok) {
    auto token = reader.next();
    if (token == ebus::detail::JsonReader::Token::object_end ||
        token == ebus::detail::JsonReader::Token::end)
      break;
    if (token == ebus::detail::JsonReader::Token::key) {
      std::string key(reader.value());
      if (reader.next() == ebus::detail::JsonReader::Token::string) {
        if (!::writeString(handle, key.c_str(), std::string(reader.value()),
                           error)) {
          ok = false;
        }
        dirty = true;
      } else {
        error = "Unsupported value type for key '" + key + "'";
        ok = false;
      }
    }
  }

  if (dirty && ok) {
    nvs_commit(handle);
  }
  nvs_close(handle);
  return ok;
}

esp_err_t ConfigManager::handleGet(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  fetchConfig([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t ConfigManager::handleSet(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "config_set",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();
  std::string error;
  bool success = writeConfigJson(sr.jsonReader().remaining(), error);
  if (success) {
    HttpUtils::sendSuccessResponse(req, "config_set", "successful",
                                   "Config saved to NVS");
  } else {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "config_set", error);
  }
  return ESP_OK;
}

esp_err_t ConfigManager::handleReset(httpd_req_t* req) {
  resetConfig();
  HttpUtils::sendSuccessResponse(req, "config_reset", "successful",
                                 "Config reset");
  return ESP_OK;
}
