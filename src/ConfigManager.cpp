#include "ConfigManager.hpp"

#include <ArduinoJson.h>
#include <cstdio>
#include <cstdlib>
#include <esp_err.h>
#include <nvs.h>
#include <vector>

namespace {

constexpr const char* kNvsNamespace = "esp-ebus";

String readString(nvs_handle_t handle, const char* key, const char* fallback = "") {
  size_t required = 0;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &required);
  if (err == ESP_ERR_NVS_NOT_FOUND) return String(fallback);
  if (err != ESP_OK || required == 0) return String(fallback);

  std::vector<char> buffer(required, '\0');
  err = nvs_get_str(handle, key, buffer.data(), &required);
  if (err != ESP_OK) return String(fallback);
  return String(buffer.data());
}

bool writeString(nvs_handle_t handle, const char* key, const String& value,
                 String& error) {
  const esp_err_t err = nvs_set_str(handle, key, value.c_str());
  if (err != ESP_OK) {
    error = String("Failed to write key '") + key + "': " + esp_err_to_name(err);
    return false;
  }
  return true;
}

bool parseStoredBool(const String& value) {
  return value == "selected" || value == "true" || value == "1" ||
         value == "on";
}

bool readEntryValueAsString(nvs_handle_t handle, const nvs_entry_info_t& info,
                            String& out) {
  switch (info.type) {
    case NVS_TYPE_STR: {
      out = readString(handle, info.key);
      return true;
    }
    case NVS_TYPE_I8: {
      int8_t value = 0;
      if (nvs_get_i8(handle, info.key, &value) != ESP_OK) return false;
      out = String(value);
      return true;
    }
    case NVS_TYPE_U8: {
      uint8_t value = 0;
      if (nvs_get_u8(handle, info.key, &value) != ESP_OK) return false;
      out = String(value);
      return true;
    }
    case NVS_TYPE_I16: {
      int16_t value = 0;
      if (nvs_get_i16(handle, info.key, &value) != ESP_OK) return false;
      out = String(value);
      return true;
    }
    case NVS_TYPE_U16: {
      uint16_t value = 0;
      if (nvs_get_u16(handle, info.key, &value) != ESP_OK) return false;
      out = String(value);
      return true;
    }
    case NVS_TYPE_I32: {
      int32_t value = 0;
      if (nvs_get_i32(handle, info.key, &value) != ESP_OK) return false;
      out = String(value);
      return true;
    }
    case NVS_TYPE_U32: {
      uint32_t value = 0;
      if (nvs_get_u32(handle, info.key, &value) != ESP_OK) return false;
      out = String(value);
      return true;
    }
    case NVS_TYPE_I64: {
      int64_t value = 0;
      if (nvs_get_i64(handle, info.key, &value) != ESP_OK) return false;
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
      out = String(buffer);
      return true;
    }
    case NVS_TYPE_U64: {
      uint64_t value = 0;
      if (nvs_get_u64(handle, info.key, &value) != ESP_OK) return false;
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%llu",
               static_cast<unsigned long long>(value));
      out = String(buffer);
      return true;
    }
    default:
      return false;
  }
}

void fillJsonFromNvs(JsonDocument& doc, nvs_handle_t handle) {
  JsonObject jConfig = doc["config"].to<JsonObject>();
  nvs_iterator_t it = nvs_entry_find("nvs", kNvsNamespace, NVS_TYPE_ANY);
  while (it != nullptr) {
    nvs_entry_info_t info{};
    nvs_entry_info(it, &info);

    String value;
    if (readEntryValueAsString(handle, info, value)) {
      jConfig[info.key] = value;
    }

    it = nvs_entry_next(it);
  }
  nvs_release_iterator(it);
}

bool writeFromFlatPayload(JsonDocument& bodyDoc, nvs_handle_t handle, String& error,
                          bool& dirty) {
  for (JsonPair kv : bodyDoc.as<JsonObject>()) {
    if (kv.value().is<String>()) {
      if (!writeString(handle, kv.key().c_str(), kv.value().as<String>(), error)) {
        return false;
      }
      dirty = true;
    } else {
      error = String("Unsupported value type for key '") + kv.key().c_str() + "'";
      return false;
    }
  }

  return true;
}

}  // namespace

String ConfigManager::readString(const char* key, const char* fallback) {
  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (openErr != ESP_OK) return String(fallback);

  String value = ::readString(handle, key, fallback);
  nvs_close(handle);
  return value;
}

int32_t ConfigManager::readInt(const char* key, int32_t fallback) {
  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (openErr != ESP_OK) return fallback;

  int32_t value = fallback;
  esp_err_t err = nvs_get_i32(handle, key, &value);
  if (err == ESP_OK) {
    nvs_close(handle);
    return value;
  }

  // Backward compatibility for values stored as strings.
  String strValue = ::readString(handle, key);
  nvs_close(handle);
  if (strValue.isEmpty()) return fallback;

  char* end = nullptr;
  const long parsed = std::strtol(strValue.c_str(), &end, 10);
  if (end == strValue.c_str() || *end != '\0') return fallback;
  return static_cast<int32_t>(parsed);
}

bool ConfigManager::readBool(const char* key, bool fallback) {
  return parseStoredBool(readString(key, fallback ? "selected" : ""));
}

bool ConfigManager::writeString(const char* key, const String& value) {
  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (openErr != ESP_OK) return false;

  String error;
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
  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (openErr != ESP_OK) return;

  const esp_err_t eraseErr = nvs_erase_all(handle);
  if (eraseErr == ESP_OK) {
    nvs_commit(handle);
  }

  nvs_close(handle);
}

void ConfigManager::begin(WebServer* server) {
  server_ = server;

  server_->on("/api/v1/config", HTTP_GET, [this]() { handleGet(); });
  server_->on("/api/v1/config", HTTP_POST, [this]() { handleSet(); });
  server_->on("/api/v1/config/reset", HTTP_POST, [this]() { handleReset(); });
}

String ConfigManager::readConfigJson() {
  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (openErr != ESP_OK) return "{}";

  JsonDocument doc;
  fillJsonFromNvs(doc, handle);
  nvs_close(handle);

  String payload;
  serializeJson(doc, payload);
  return payload;
}

bool ConfigManager::writeConfigJson(const String& body, String& error) {
  JsonDocument bodyDoc;
  DeserializationError parseError = deserializeJson(bodyDoc, body);
  if (parseError) {
    error = String("Invalid JSON: ") + parseError.c_str();
    return false;
  }

  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (openErr != ESP_OK) {
    error = String("Failed to open NVS: ") + esp_err_to_name(openErr);
    return false;
  }

  bool dirty = false;
  bool ok = writeFromFlatPayload(bodyDoc, handle, error, dirty);
  if (!ok) {
    nvs_close(handle);
    return false;
  }

  if (dirty) {
    const esp_err_t commitErr = nvs_commit(handle);
    if (commitErr != ESP_OK) {
      nvs_close(handle);
      error = String("Failed to commit NVS: ") + esp_err_to_name(commitErr);
      return false;
    }
  }
  nvs_close(handle);
  return true;
}

void ConfigManager::handleGet() {
  server_->send(200, "application/json;charset=utf-8", readConfigJson());
}

void ConfigManager::handleSet() {
  String error;
  const String body = server_->arg("plain");
  if (!writeConfigJson(body, error)) {
    server_->send(400, "text/plain", error);
    return;
  }

  server_->send(200, "text/plain", "Config saved to NVS");
}

void ConfigManager::handleReset() {
  resetConfig();
  server_->send(200, "text/plain", "Config reset");
}
