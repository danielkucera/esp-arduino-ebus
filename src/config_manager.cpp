#include "config_manager.hpp"

#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstdio>
#include <cstdlib>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>
#include <array>
#include <string>

#include "http.hpp"
#include "http_utils.hpp"
#include "legacy_config.hpp"

extern ConfigManager configManager;

namespace {

constexpr const char* nvs_namespace = "esp-ebus";
constexpr const char* legacy_eeprom_namespace = "eeprom";
constexpr const char* legacy_eeprom_key = "eeprom";
constexpr const char* migration_namespace = "ebus-migrate";
constexpr const char* migration_key = "iotwebconf";


bool ensureNvsReady() {
  static bool nvsReady = false;
  if (nvsReady) return true;

  esp_err_t err = nvs_flash_init();
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

bool hasNvsEntry(const char* key) {
  nvs_iterator_t it = nullptr;
  if (nvs_entry_find("nvs", nvs_namespace, NVS_TYPE_ANY, &it) != ESP_OK) {
    return false;
  }

  bool found = false;
  while (it != nullptr) {
    nvs_entry_info_t info{};
    nvs_entry_info(it, &info);
    if (std::strcmp(info.key, key) == 0) {
      found = true;
      break;
    }
    if (nvs_entry_next(&it) != ESP_OK) break;
  }
  nvs_release_iterator(it);
  return found;
}

bool loadLegacyIotWebConf(LegacyIotWebConfConfig& legacy) {
  nvs_handle_t handle = 0;
  if (nvs_open(legacy_eeprom_namespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }

  size_t size = 0;
  esp_err_t err = nvs_get_blob(handle, legacy_eeprom_key, nullptr, &size);
  constexpr size_t minimum_size = 169;
  constexpr size_t maximum_size = 512;
  if (err != ESP_OK || size < minimum_size || size > maximum_size) {
    nvs_close(handle);
    return false;
  }

  std::array<uint8_t, maximum_size> data{};
  err = nvs_get_blob(handle, legacy_eeprom_key, data.data(), &size);
  nvs_close(handle);
  return err == ESP_OK && parseLegacyIotWebConfConfig(data.data(), size, legacy);
}

bool legacyMigrationComplete() {
  nvs_handle_t handle = 0;
  if (nvs_open(migration_namespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }
  uint8_t complete = 0;
  const esp_err_t err = nvs_get_u8(handle, migration_key, &complete);
  nvs_close(handle);
  return err == ESP_OK && complete == 1;
}

bool markLegacyMigrationComplete() {
  nvs_handle_t handle = 0;
  if (nvs_open(migration_namespace, NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }
  bool ok = nvs_set_u8(handle, migration_key, 1) == ESP_OK;
  if (ok) ok = nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

bool eraseLegacyIotWebConf() {
  nvs_handle_t handle = 0;
  const esp_err_t openErr =
      nvs_open(legacy_eeprom_namespace, NVS_READWRITE, &handle);
  if (openErr == ESP_ERR_NVS_NOT_FOUND) return true;
  if (openErr != ESP_OK) return false;

  esp_err_t err = nvs_erase_key(handle, legacy_eeprom_key);
  if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err == ESP_OK;
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

bool ConfigManager::migrateLegacyConfig() {
  if (!ensureNvsReady()) return false;
  if (legacyMigrationComplete()) return true;

  LegacyIotWebConfConfig legacy;
  const bool hasLegacyIotWebConf = loadLegacyIotWebConf(legacy);

  nvs_handle_t handle = 0;
  if (nvs_open(nvs_namespace, NVS_READWRITE, &handle) != ESP_OK) return false;

  uint32_t legacyPwm = 0;
  const bool hasLegacyPwm =
      nvs_get_u32(handle, "pwm_value", &legacyPwm) == ESP_OK;
  if (!hasLegacyIotWebConf && !hasLegacyPwm) {
    nvs_close(handle);
    return true;
  }
  if (!hasLegacyIotWebConf && hasLegacyPwm && !hasNvsEntry("wifiSsid")) {
    nvs_close(handle);
    return false;
  }

  bool dirty = false;
  bool ok = true;
  std::string error;
  auto migrateString = [&](const char* key, const std::string& value,
                           bool allowEmpty = false) {
    if (!ok || (!allowEmpty && value.empty()) || hasNvsEntry(key)) return;
    ok = ::writeString(handle, key, value, error);
    dirty |= ok;
  };

  if (hasLegacyIotWebConf) {
    migrateString("thingName", legacy.thing_name);
    if (legacyAdminPasswordIsValid(legacy.admin_password)) {
      migrateString("apModePassword", legacy.admin_password);
    }
    migrateString("wifiSsid", legacy.wifi_ssid, true);
    migrateString("wifiPassword", legacy.wifi_password, true);
    migrateString("wifiPowerSave", "false");
  }

  if (ok && hasLegacyPwm && !hasNvsEntry("pwmValue")) {
    if (legacyPwmIsValid(legacyPwm)) {
      ok = nvs_set_i32(handle, "pwmValue", static_cast<int32_t>(legacyPwm)) ==
           ESP_OK;
      dirty |= ok;
    }
  }

  if (ok && dirty) ok = nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok && markLegacyMigrationComplete();
}


void ConfigManager::resetConfig() {
  if (!ensureNvsReady()) return;

  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(nvs_namespace, NVS_READWRITE, &handle);
  if (openErr != ESP_OK) return;

  esp_err_t resetErr = nvs_erase_all(handle);
  if (resetErr == ESP_OK) resetErr = nvs_commit(handle);

  nvs_close(handle);
  if (resetErr == ESP_OK) eraseLegacyIotWebConf();
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
