#include "http.hpp"

#include <cJSON.h>
#include <cstdio>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>
#include <vector>

#include "ConfigManager.hpp"
#include "DeviceManager.hpp"
#include "HttpUtils.hpp"
#include "Logger.hpp"
#include "MqttHA.hpp"
#include "Schedule.hpp"
#include "Store.hpp"
#include "WifiNetworkManager.hpp"
#include "main.hpp"

static httpd_handle_t configServer = nullptr;
static bool fallbackHandlersRegistered = false;

namespace {
extern const char common_css_start[] asm("_binary_common_css_start");
extern const char common_js_start[] asm("_binary_common_js_start");

extern const char root_html_start[] asm("_binary_root_html_start");
extern const char status_html_start[] asm("_binary_status_html_start");
extern const char config_html_start[] asm("_binary_config_html_start");
extern const char upgrade_html_start[] asm("_binary_upgrade_html_start");
extern const char commands_html_start[] asm("_binary_commands_html_start");
extern const char values_html_start[] asm("_binary_values_html_start");
extern const char devices_html_start[] asm("_binary_devices_html_start");
extern const char statistics_html_start[] asm("_binary_statistics_html_start");
extern const char logs_html_start[] asm("_binary_logs_html_start");

void sendStatic(httpd_req_t* req, const char* contentType, const char* data) {
  HttpUtils::sendResponse(req, "200 OK", contentType, std::string(data));
}

esp_err_t handleRoot(httpd_req_t* req) {
  sendStatic(req, "text/html", root_html_start);
  return ESP_OK;
}

esp_err_t handleStatusPage(httpd_req_t* req) {
  sendStatic(req, "text/html", status_html_start);
  return ESP_OK;
}

esp_err_t handleStatusApi(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8",
                          getStatusJson());
  return ESP_OK;
}

esp_err_t handleConfigPage(httpd_req_t* req) {
  sendStatic(req, "text/html", config_html_start);
  return ESP_OK;
}

esp_err_t handleUpgradePage(httpd_req_t* req) {
  sendStatic(req, "text/html", upgrade_html_start);
  return ESP_OK;
}

esp_err_t handleCommonCss(httpd_req_t* req) {
  sendStatic(req, "text/css", common_css_start);
  return ESP_OK;
}

esp_err_t handleCommonJs(httpd_req_t* req) {
  sendStatic(req, "application/javascript", common_js_start);
  return ESP_OK;
}

esp_err_t handleRestart(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", "Restarting...");
  vTaskDelay(pdMS_TO_TICKS(500));
  restart();
  return ESP_OK;
}

esp_err_t handleWifiScan(httpd_req_t* req) {
  // Start WiFi scan
  wifi_scan_config_t scanConfig = {
    .ssid = nullptr,
    .bssid = nullptr,
    .channel = 0,
    .show_hidden = true,
    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    .scan_time = {
      .active = {
        .min = 0,
        .max = 0
      },
      .passive = 100
    }
  };

  esp_err_t err = esp_wifi_scan_start(&scanConfig, true);
  if (err != ESP_OK) {
    HttpUtils::sendResponse(req, "500 Internal Server Error", "application/json",
                           "{\"error\":\"WiFi scan failed\"}");
    return ESP_OK;
  }

  // Get scan results
  uint16_t apCount = 0;
  esp_wifi_scan_get_ap_num(&apCount);

  if (apCount == 0) {
    HttpUtils::sendResponse(req, "200 OK", "application/json", "[]");
    return ESP_OK;
  }

  std::vector<wifi_ap_record_t> aps(apCount);
  esp_wifi_scan_get_ap_records(&apCount, aps.data());

  // Build JSON response
  cJSON* root = cJSON_CreateArray();

  for (uint16_t i = 0; i < apCount; ++i) {
    const wifi_ap_record_t& ap = aps[i];
    
    cJSON* item = cJSON_CreateObject();
    
    // SSID
    std::string ssid(reinterpret_cast<const char*>(ap.ssid),
                    strnlen(reinterpret_cast<const char*>(ap.ssid), 32));
    cJSON_AddStringToObject(item, "ssid", ssid.c_str());
    
    // BSSID (MAC address)
    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             ap.bssid[0], ap.bssid[1], ap.bssid[2],
             ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    cJSON_AddStringToObject(item, "bssid", bssidStr);
    
    // Signal strength (RSSI)
    cJSON_AddNumberToObject(item, "rssi", ap.rssi);
    
    // Channel
    cJSON_AddNumberToObject(item, "channel", ap.primary);
    
    // Security type
    const char* authMode = "UNKNOWN";
    switch (ap.authmode) {
      case WIFI_AUTH_OPEN: authMode = "OPEN"; break;
      case WIFI_AUTH_WEP: authMode = "WEP"; break;
      case WIFI_AUTH_WPA_PSK: authMode = "WPA_PSK"; break;
      case WIFI_AUTH_WPA2_PSK: authMode = "WPA2_PSK"; break;
      case WIFI_AUTH_WPA_WPA2_PSK: authMode = "WPA_WPA2_PSK"; break;
      case WIFI_AUTH_WPA2_ENTERPRISE: authMode = "WPA2_ENTERPRISE"; break;
      case WIFI_AUTH_WPA3_PSK: authMode = "WPA3_PSK"; break;
      case WIFI_AUTH_WPA2_WPA3_PSK: authMode = "WPA2_WPA3_PSK"; break;
      default: break;
    }
    cJSON_AddStringToObject(item, "authMode", authMode);
    
    cJSON_AddItemToArray(root, item);
  }

  // Convert JSON to string and send response
  char* jsonString = cJSON_Print(root);
  HttpUtils::sendResponse(req, "200 OK", "application/json", jsonString);
  
  cJSON_Delete(root);
  free(jsonString);
  
  esp_wifi_clear_ap_list();

  return ESP_OK;
}

#if defined(EBUS_INTERNAL)
esp_err_t handleCommandsPage(httpd_req_t* req) {
  sendStatic(req, "text/html", commands_html_start);
  return ESP_OK;
}

esp_err_t handleCommands(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8",
                          store.getCommandsJson());
  return ESP_OK;
}

esp_err_t handleCommandsEvaluate(httpd_req_t* req) {
  cJSON* doc = cJSON_Parse(HttpUtils::readBody(req).c_str());
  if (!cJSON_IsArray(doc)) {
    HttpUtils::sendResponse(req, "403 Forbidden", "text/html", "Json invalid");
  } else {
    cJSON* command = nullptr;
    cJSON_ArrayForEach(command, doc) {
      std::string evalError = Command::evaluate(command);
      if (!evalError.empty()) {
        cJSON_Delete(doc);
        HttpUtils::sendResponse(req, "403 Forbidden", "text/html", evalError.c_str());
        return ESP_OK;
      }
    }
    HttpUtils::sendResponse(req, "200 OK", "text/html", "Ok");
  }
  if (doc) cJSON_Delete(doc);
  return ESP_OK;
}

esp_err_t handleCommandsInsert(httpd_req_t* req) {
  cJSON* doc = cJSON_Parse(HttpUtils::readBody(req).c_str());
  if (!cJSON_IsArray(doc)) {
    HttpUtils::sendResponse(req, "403 Forbidden", "text/html", "Json invalid");
  } else {
    cJSON* command = nullptr;
    cJSON_ArrayForEach(command, doc) {
      std::string evalError = Command::evaluate(command);
      if (evalError.empty()) {
        store.insertCommand(Command::fromJson(command));
      } else {
        cJSON_Delete(doc);
        HttpUtils::sendResponse(req, "403 Forbidden", "text/html", evalError.c_str());
        return ESP_OK;
      }
    }
    if (mqttha.isEnabled()) mqttha.publishComponents();
    HttpUtils::sendResponse(req, "200 OK", "text/html", "Ok");
  }
  if (doc) cJSON_Delete(doc);
  return ESP_OK;
}

esp_err_t handleCommandsRemove(httpd_req_t* req) {
  cJSON* doc = cJSON_Parse(HttpUtils::readBody(req).c_str());
  if (!cJSON_IsObject(doc)) {
    HttpUtils::sendResponse(req, "403 Forbidden", "text/html", "Json invalid");
  } else {
    cJSON* keys = cJSON_GetObjectItemCaseSensitive(doc, "keys");
    if (cJSON_IsArray(keys) && cJSON_GetArraySize(keys) > 0) {
      cJSON* key = nullptr;
      cJSON_ArrayForEach(key, keys) {
        if (!cJSON_IsString(key) || key->valuestring == nullptr) continue;
        const Command* cmd = store.findCommand(key->valuestring);
        if (cmd) {
          if (mqttha.isEnabled()) mqttha.publishComponent(cmd, true);
          store.removeCommand(key->valuestring);
        }
      }
      HttpUtils::sendResponse(req, "200 OK", "text/html", "Ok");
    } else if (store.getActiveCommands() + store.getPassiveCommands() > 0) {
      for (const Command* cmd : store.getCommands()) {
        if (mqttha.isEnabled()) mqttha.publishComponent(cmd, true);
        store.removeCommand(cmd->getKey());
      }
      HttpUtils::sendResponse(req, "200 OK", "text/html", "Ok");
    } else {
      HttpUtils::sendResponse(req, "403 Forbidden", "text/html", "No commands");
    }
  }
  if (doc) cJSON_Delete(doc);
  return ESP_OK;
}

esp_err_t handleCommandsLoad(httpd_req_t* req) {
  int64_t bytes = store.loadCommands();
  if (bytes > 0)
    HttpUtils::sendResponse(req, "200 OK", "text/html",
                            std::to_string(bytes) + " bytes loaded");
  else if (bytes < 0)
    HttpUtils::sendResponse(req, "200 OK", "text/html", "Loading failed");
  else
    HttpUtils::sendResponse(req, "200 OK", "text/html", "No data loaded");

  if (mqttha.isEnabled()) mqttha.publishComponents();
  return ESP_OK;
}

esp_err_t handleCommandsSave(httpd_req_t* req) {
  int64_t bytes = store.saveCommands();
  if (bytes > 0)
    HttpUtils::sendResponse(req, "200 OK", "text/html",
                            std::to_string(bytes) + " bytes saved");
  else if (bytes < 0)
    HttpUtils::sendResponse(req, "200 OK", "text/html", "Saving failed");
  else
    HttpUtils::sendResponse(req, "200 OK", "text/html", "No data saved");
  return ESP_OK;
}

esp_err_t handleCommandsWipe(httpd_req_t* req) {
  int64_t bytes = store.wipeCommands();
  if (bytes > 0)
    HttpUtils::sendResponse(req, "200 OK", "text/html",
                            std::to_string(bytes) + " bytes wiped");
  else if (bytes < 0)
    HttpUtils::sendResponse(req, "200 OK", "text/html", "Wiping failed");
  else
    HttpUtils::sendResponse(req, "200 OK", "text/html", "No data wiped");
  return ESP_OK;
}

esp_err_t handleValuesPage(httpd_req_t* req) {
  sendStatic(req, "text/html", values_html_start);
  return ESP_OK;
}

esp_err_t handleValues(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8",
                          store.getValuesJson());
  return ESP_OK;
}

esp_err_t handleValuesWrite(httpd_req_t* req) {
  cJSON* doc = cJSON_Parse(HttpUtils::readBody(req).c_str());
  if (!cJSON_IsObject(doc)) {
    HttpUtils::sendResponse(req, "403 Forbidden", "text/html", "Json invalid");
  } else {
    cJSON* keyNode = cJSON_GetObjectItemCaseSensitive(doc, "key");
    std::string key =
        (cJSON_IsString(keyNode) && keyNode->valuestring != nullptr)
            ? keyNode->valuestring
            : "";
    Command* command = store.findCommand(key);
    if (command != nullptr) {
      std::vector<uint8_t> valueBytes = command->getVectorFromJson(doc);
      if (!valueBytes.empty()) {
        std::vector<uint8_t> writeCmd = command->getWriteCmd();
        writeCmd.insert(writeCmd.end(), valueBytes.begin(), valueBytes.end());
        schedule.handleWrite(writeCmd);
        HttpUtils::sendResponse(req, "200 OK", "text/html", "Ok");
      } else {
        HttpUtils::sendResponse(
            req, "403 Forbidden", "text/html",
            std::string("Invalid value for key '") + key + "'");
      }
    } else {
      HttpUtils::sendResponse(
          req, "403 Forbidden", "text/html",
          std::string("Key '") + key + "' not found");
    }
  }
  if (doc) cJSON_Delete(doc);
  return ESP_OK;
}

esp_err_t handleValuesRead(httpd_req_t* req) {
  cJSON* doc = cJSON_Parse(HttpUtils::readBody(req).c_str());
  if (!cJSON_IsObject(doc)) {
    cJSON* errDoc = cJSON_CreateObject();
    cJSON_AddStringToObject(errDoc, "id", "read");
    cJSON_AddStringToObject(errDoc, "status", "invalid json payload");
    char* printed = cJSON_PrintUnformatted(errDoc);
    std::string payload = printed != nullptr ? printed : "{}";
    if (printed != nullptr) cJSON_free(printed);
    cJSON_Delete(errDoc);
    HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", payload.c_str());
  } else {
    cJSON* keyNode = cJSON_GetObjectItemCaseSensitive(doc, "key");
    std::string key =
        (cJSON_IsString(keyNode) && keyNode->valuestring != nullptr)
            ? keyNode->valuestring
            : "";
    Command* command = store.findCommand(key);
    if (command != nullptr) {
      command->setLast(0);
      cJSON* resp = cJSON_CreateObject();
      cJSON_AddStringToObject(resp, "id", "read");
      cJSON_AddStringToObject(resp, "status", "requested");
      char* printed = cJSON_PrintUnformatted(resp);
      std::string payload = printed != nullptr ? printed : "{}";
      if (printed != nullptr) cJSON_free(printed);
      cJSON_Delete(resp);
      HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", payload.c_str());
    } else {
      cJSON* errDoc = cJSON_CreateObject();
      cJSON_AddStringToObject(errDoc, "id", "read");
      cJSON_AddStringToObject(errDoc, "status",
                              (std::string("Key '") + key + "' not found").c_str());
      char* printed = cJSON_PrintUnformatted(errDoc);
      std::string payload = printed != nullptr ? printed : "{}";
      if (printed != nullptr) cJSON_free(printed);
      cJSON_Delete(errDoc);
      HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", payload.c_str());
    }
  }
  if (doc) cJSON_Delete(doc);
  return ESP_OK;
}

esp_err_t handleDevicesPage(httpd_req_t* req) {
  sendStatic(req, "text/html", devices_html_start);
  return ESP_OK;
}

esp_err_t handleDevices(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8",
                          deviceManager.getDevicesJson());
  return ESP_OK;
}

esp_err_t handleDevicesScan(httpd_req_t* req) {
  schedule.handleScan();
  HttpUtils::sendResponse(req, "200 OK", "text/html", "Scan initiated");
  return ESP_OK;
}

esp_err_t handleDevicesScanFull(httpd_req_t* req) {
  schedule.handleScanFull();
  HttpUtils::sendResponse(req, "200 OK", "text/html", "Full scan initiated");
  return ESP_OK;
}

esp_err_t handleDevicesScanVendor(httpd_req_t* req) {
  schedule.handleScanVendor();
  HttpUtils::sendResponse(req, "200 OK", "text/html", "Vendor scan initiated");
  return ESP_OK;
}

esp_err_t handleStatisticsPage(httpd_req_t* req) {
  sendStatic(req, "text/html", statistics_html_start);
  return ESP_OK;
}

esp_err_t handleStatisticsCounter(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8",
                          schedule.getCounterJson());
  return ESP_OK;
}

esp_err_t handleStatisticsTiming(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8",
                          schedule.getTimingJson());
  return ESP_OK;
}

esp_err_t handleStatisticsReset(httpd_req_t* req) {
  deviceManager.resetAddresses();
  schedule.resetCounter();
  schedule.resetTiming();
  HttpUtils::sendResponse(req, "200 OK", "text/html", "Statistics reset");
  return ESP_OK;
}

esp_err_t handleLogsPage(httpd_req_t* req) {
  sendStatic(req, "text/html", logs_html_start);
  return ESP_OK;
}

esp_err_t handleLogs(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/plain", logger.getLogs());
  return ESP_OK;
}
#endif

esp_err_t handleNotFound(httpd_req_t* req) {
  if (!WifiNetworkManager::isStaConnected() &&
      WifiNetworkManager::getMode() != WIFI_MODE_STA) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Location", "/config");
    httpd_resp_send(req, "", 0);
    return ESP_OK;
  }

  HttpUtils::sendResponse(req, "404 Not Found", "text/plain", "Not found");
  return ESP_OK;
}

}  // namespace

httpd_handle_t GetHttpServer() { return configServer; }

bool RegisterUri(const char* uri, httpd_method_t method,
                 esp_err_t (*handler)(httpd_req_t*)) {
  if (configServer == nullptr) {
    logger.error(std::string("HTTP server not started; cannot register ") + uri);
    return false;
  }
  return HttpUtils::registerRoute(configServer, uri, method, handler);
}

void SetupHttpHandlers() {
  if (configServer != nullptr) return;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 64;
  config.stack_size = 12288;

  if (httpd_start(&configServer, &config) != ESP_OK) {
    logger.error("Failed to start HTTP server");
    return;
  }

  RegisterUri("/common.css", HTTP_GET, handleCommonCss);
  RegisterUri("/common.js", HTTP_GET, handleCommonJs);
  RegisterUri("/", HTTP_GET, handleRoot);
  RegisterUri("/config", HTTP_GET, handleConfigPage);
  RegisterUri("/status", HTTP_GET, handleStatusPage);
  RegisterUri("/api/v1/status", HTTP_GET, handleStatusApi);
  RegisterUri("/api/v1/wifi/scan", HTTP_POST, handleWifiScan);
  RegisterUri("/upgrade", HTTP_GET, handleUpgradePage);

#if defined(EBUS_INTERNAL)
  RegisterUri("/commands", HTTP_GET, handleCommandsPage);
  RegisterUri("/api/v1/commands", HTTP_GET, handleCommands);
  RegisterUri("/api/v1/commands/evaluate", HTTP_POST, handleCommandsEvaluate);
  RegisterUri("/api/v1/commands/insert", HTTP_POST, handleCommandsInsert);
  RegisterUri("/api/v1/commands/remove", HTTP_POST, handleCommandsRemove);
  RegisterUri("/api/v1/commands/load", HTTP_POST, handleCommandsLoad);
  RegisterUri("/api/v1/commands/save", HTTP_POST, handleCommandsSave);
  RegisterUri("/api/v1/commands/wipe", HTTP_POST, handleCommandsWipe);

  RegisterUri("/values", HTTP_GET, handleValuesPage);
  RegisterUri("/api/v1/values", HTTP_GET, handleValues);
  RegisterUri("/api/v1/values/write", HTTP_POST, handleValuesWrite);
  RegisterUri("/api/v1/values/read", HTTP_POST, handleValuesRead);

  RegisterUri("/devices", HTTP_GET, handleDevicesPage);
  RegisterUri("/api/v1/devices", HTTP_GET, handleDevices);
  RegisterUri("/api/v1/devices/scan", HTTP_POST, handleDevicesScan);
  RegisterUri("/api/v1/devices/scan/full", HTTP_POST, handleDevicesScanFull);
  RegisterUri("/api/v1/devices/scan/vendor", HTTP_POST, handleDevicesScanVendor);

  RegisterUri("/statistics", HTTP_GET, handleStatisticsPage);
  RegisterUri("/api/v1/statistics/counter", HTTP_GET, handleStatisticsCounter);
  RegisterUri("/api/v1/statistics/timing", HTTP_GET, handleStatisticsTiming);
  RegisterUri("/api/v1/statistics/reset", HTTP_POST, handleStatisticsReset);

  RegisterUri("/logs", HTTP_GET, handleLogsPage);
  RegisterUri("/api/v1/logs", HTTP_GET, handleLogs);
#endif

  RegisterUri("/restart", HTTP_GET, handleRestart);
}

void SetupHttpFallbackHandlers() {
  if (configServer == nullptr || fallbackHandlersRegistered) return;
  RegisterUri("/*", HTTP_GET, handleNotFound);
  RegisterUri("/*", HTTP_POST, handleNotFound);
  fallbackHandlersRegistered = true;
}
