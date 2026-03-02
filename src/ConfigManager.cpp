#include "ConfigManager.hpp"

#include <ArduinoJson.h>
#include <esp_err.h>
#include <nvs.h>
#include <vector>

namespace {

constexpr const char* kNvsNamespace = "iwcAll";

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

bool writeSelectedFlag(nvs_handle_t handle, const char* key, bool enabled,
                       String& error) {
  return writeString(handle, key, enabled ? "selected" : "", error);
}

void putSelectedFromString(JsonDocument& doc, const char* field, const String& value) {
  doc[field] = value == "selected";
}

void fillJsonFromNvs(JsonDocument& doc, nvs_handle_t handle) {
  JsonObject iwcSys = doc["iwcSys"].to<JsonObject>();
  iwcSys["iwcThingName"] = readString(handle, "iwcThingName", "esp-eBus");
  iwcSys["iwcApPassword"] = readString(handle, "iwcApPassword");
  iwcSys["iwcApTimeout"] = readString(handle, "iwcApTimeout", "30");

  JsonObject iwcWifi0 = iwcSys["iwcWifi0"].to<JsonObject>();
  iwcWifi0["iwcWifiSsid"] = readString(handle, "iwcWifiSsid");
  iwcWifi0["iwcWifiPassword"] = readString(handle, "iwcWifiPassword");

  JsonObject iwcCustom = doc["iwcCustom"].to<JsonObject>();
  JsonObject conn = iwcCustom["conn"].to<JsonObject>();
  const String staticIP = readString(handle, "staticIPParam");
  conn["staticIPParam"] = staticIP;
  conn["ipAddress"] = readString(handle, "ipAddress");
  conn["gateway"] = readString(handle, "gateway");
  conn["netmask"] = readString(handle, "netmask");

  JsonObject sntp = iwcCustom["sntp"].to<JsonObject>();
  sntp["sntpEnabled"] = readString(handle, "sntpEnabled");
  sntp["sntpServer"] = readString(handle, "sntpServer", "sk.pool.ntp.org");
  sntp["sntpTimezone"] = readString(handle, "sntpTimezone", "UTC+1");

  JsonObject ebus = iwcCustom["ebus"].to<JsonObject>();
  ebus["pwm_value"] = readString(handle, "pwm_value", "120");
  ebus["ebus_address"] = readString(handle, "ebus_address", "ff");
  ebus["busisr_window"] = readString(handle, "busisr_window");
  ebus["busisr_offset"] = readString(handle, "busisr_offset");

  JsonObject schedule = iwcCustom["schedule"].to<JsonObject>();
  schedule["inquiryOfExistenceParam"] = readString(handle, "inquiryOfExistenceParam");
  schedule["scanOnStartupParam"] = readString(handle, "scanOnStartupParam");
  schedule["firstCommandAfterStartParam"] = readString(handle, "firstCommandAfterStartParam");

  JsonObject mqtt = iwcCustom["mqtt"].to<JsonObject>();
  mqtt["mqttEnabledParam"] = readString(handle, "mqttEnabledParam");
  mqtt["mqtt_server"] = readString(handle, "mqtt_server");
  mqtt["mqtt_user"] = readString(handle, "mqtt_user");
  mqtt["mqtt_pass"] = readString(handle, "mqtt_pass");
  mqtt["mqttPublishCounterParam"] = readString(handle, "mqttPublishCounterParam");
  mqtt["mqttPublishTimingParam"] = readString(handle, "mqttPublishTimingParam");

  JsonObject ha = iwcCustom["ha"].to<JsonObject>();
  ha["haEnabledParam"] = readString(handle, "haEnabledParam");

  // Compatibility with current config2.html flat payload.
  doc["wifiSsid"] = iwcWifi0["iwcWifiSsid"];
  doc["wifiPassword"] = iwcWifi0["iwcWifiPassword"];
  doc["apPassword"] = iwcSys["iwcApPassword"];
  putSelectedFromString(doc, "staticIP", staticIP);
  doc["ipAddress"] = conn["ipAddress"];
  doc["gateway"] = conn["gateway"];
  doc["netmask"] = conn["netmask"];
  doc["pwm"] = ebus["pwm_value"];
}

bool writeFromFlatPayload(JsonDocument& bodyDoc, nvs_handle_t handle, String& error,
                          bool& dirty) {
  if (bodyDoc["wifiSsid"].is<String>()) {
    if (!writeString(handle, "iwcWifiSsid", bodyDoc["wifiSsid"].as<String>(), error)) return false;
    dirty = true;
  }
  if (bodyDoc["wifiPassword"].is<String>()) {
    if (!writeString(handle, "iwcWifiPassword", bodyDoc["wifiPassword"].as<String>(), error)) return false;
    dirty = true;
  }
  if (bodyDoc["apPassword"].is<String>()) {
    if (!writeString(handle, "iwcApPassword", bodyDoc["apPassword"].as<String>(), error)) return false;
    dirty = true;
  }
  if (bodyDoc["staticIP"].is<bool>()) {
    if (!writeSelectedFlag(handle, "staticIPParam", bodyDoc["staticIP"].as<bool>(), error)) return false;
    dirty = true;
  }
  if (bodyDoc["ipAddress"].is<String>()) {
    if (!writeString(handle, "ipAddress", bodyDoc["ipAddress"].as<String>(), error)) return false;
    dirty = true;
  }
  if (bodyDoc["gateway"].is<String>()) {
    if (!writeString(handle, "gateway", bodyDoc["gateway"].as<String>(), error)) return false;
    dirty = true;
  }
  if (bodyDoc["netmask"].is<String>()) {
    if (!writeString(handle, "netmask", bodyDoc["netmask"].as<String>(), error)) return false;
    dirty = true;
  }
  if (bodyDoc["pwm"].is<String>()) {
    if (!writeString(handle, "pwm_value", bodyDoc["pwm"].as<String>(), error)) return false;
    dirty = true;
  } else if (bodyDoc["pwm"].is<int>()) {
    if (!writeString(handle, "pwm_value", String(bodyDoc["pwm"].as<int>()), error)) return false;
    dirty = true;
  }
  return true;
}

bool writeNestedStringIfPresent(JsonObjectConst obj, const char* key, nvs_handle_t handle,
                                String& error, bool& dirty) {
  if (!obj[key].is<String>()) return true;
  if (!writeString(handle, key, obj[key].as<String>(), error)) return false;
  dirty = true;
  return true;
}

bool writeSelectedIfPresent(JsonObjectConst obj, const char* key, nvs_handle_t handle,
                            String& error, bool& dirty) {
  if (obj[key].is<bool>()) {
    if (!writeSelectedFlag(handle, key, obj[key].as<bool>(), error)) return false;
    dirty = true;
    return true;
  }
  if (obj[key].is<String>()) {
    if (!writeString(handle, key, obj[key].as<String>(), error)) return false;
    dirty = true;
  }
  return true;
}

bool writeFromNestedPayload(JsonDocument& bodyDoc, nvs_handle_t handle, String& error,
                            bool& dirty) {
  JsonObjectConst sys = bodyDoc["iwcSys"].as<JsonObjectConst>();
  if (!sys.isNull()) {
    if (!writeNestedStringIfPresent(sys, "iwcThingName", handle, error, dirty)) return false;
    if (!writeNestedStringIfPresent(sys, "iwcApPassword", handle, error, dirty)) return false;
    if (!writeNestedStringIfPresent(sys, "iwcApTimeout", handle, error, dirty)) return false;
    JsonObjectConst wifi0 = sys["iwcWifi0"].as<JsonObjectConst>();
    if (!wifi0.isNull()) {
      if (wifi0["iwcWifiSsid"].is<String>()) {
        if (!writeString(handle, "iwcWifiSsid", wifi0["iwcWifiSsid"].as<String>(), error)) return false;
        dirty = true;
      }
      if (wifi0["iwcWifiPassword"].is<String>()) {
        if (!writeString(handle, "iwcWifiPassword", wifi0["iwcWifiPassword"].as<String>(), error)) return false;
        dirty = true;
      }
    }
  }

  JsonObjectConst custom = bodyDoc["iwcCustom"].as<JsonObjectConst>();
  if (!custom.isNull()) {
    JsonObjectConst conn = custom["conn"].as<JsonObjectConst>();
    if (!conn.isNull()) {
      if (!writeSelectedIfPresent(conn, "staticIPParam", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(conn, "ipAddress", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(conn, "gateway", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(conn, "netmask", handle, error, dirty)) return false;
    }

    JsonObjectConst sntp = custom["sntp"].as<JsonObjectConst>();
    if (!sntp.isNull()) {
      if (!writeSelectedIfPresent(sntp, "sntpEnabled", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(sntp, "sntpServer", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(sntp, "sntpTimezone", handle, error, dirty)) return false;
    }

    JsonObjectConst ebus = custom["ebus"].as<JsonObjectConst>();
    if (!ebus.isNull()) {
      if (!writeNestedStringIfPresent(ebus, "pwm_value", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(ebus, "ebus_address", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(ebus, "busisr_window", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(ebus, "busisr_offset", handle, error, dirty)) return false;
    }

    JsonObjectConst schedule = custom["schedule"].as<JsonObjectConst>();
    if (!schedule.isNull()) {
      if (!writeSelectedIfPresent(schedule, "inquiryOfExistenceParam", handle, error, dirty)) return false;
      if (!writeSelectedIfPresent(schedule, "scanOnStartupParam", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(schedule, "firstCommandAfterStartParam", handle, error, dirty)) return false;
    }

    JsonObjectConst mqtt = custom["mqtt"].as<JsonObjectConst>();
    if (!mqtt.isNull()) {
      if (!writeSelectedIfPresent(mqtt, "mqttEnabledParam", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(mqtt, "mqtt_server", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(mqtt, "mqtt_user", handle, error, dirty)) return false;
      if (!writeNestedStringIfPresent(mqtt, "mqtt_pass", handle, error, dirty)) return false;
      if (!writeSelectedIfPresent(mqtt, "mqttPublishCounterParam", handle, error, dirty)) return false;
      if (!writeSelectedIfPresent(mqtt, "mqttPublishTimingParam", handle, error, dirty)) return false;
    }

    JsonObjectConst ha = custom["ha"].as<JsonObjectConst>();
    if (!ha.isNull()) {
      if (!writeSelectedIfPresent(ha, "haEnabledParam", handle, error, dirty)) return false;
    }
  }

  return true;
}

}  // namespace

void ConfigManager::begin(WebServer* server) {
  server_ = server;

  server_->on("/api/v1/config2", HTTP_GET, [this]() { handleGet(); });
  server_->on("/api/v1/config2", HTTP_POST, [this]() { handleSet(); });
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
  bool ok = writeFromFlatPayload(bodyDoc, handle, error, dirty) &&
            writeFromNestedPayload(bodyDoc, handle, error, dirty);
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
