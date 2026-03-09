#include "http.hpp"

#include <cJSON.h>

#include "DeviceManager.hpp"
#include "HttpUtils.hpp"
#include "Logger.hpp"
#include "MqttHA.hpp"
#include "Schedule.hpp"
#include "Store.hpp"
#include "main.hpp"

namespace {
httpd_handle_t configServer = nullptr;
bool fallbackHandlersRegistered = false;

extern const char common_css_start[] asm("_binary_static_common_css_start");
extern const char common_js_start[] asm("_binary_static_common_js_start");

extern const char root_html_start[] asm("_binary_static_root_html_start");
extern const char status_html_start[] asm("_binary_static_status_html_start");
extern const char config_html_start[] asm("_binary_static_config_html_start");
extern const char upgrade_html_start[] asm("_binary_static_upgrade_html_start");
extern const char commands_html_start[] asm(
    "_binary_static_commands_html_start");
extern const char values_html_start[] asm("_binary_static_values_html_start");
extern const char devices_html_start[] asm("_binary_static_devices_html_start");
extern const char statistics_html_start[] asm(
    "_binary_static_statistics_html_start");
extern const char logs_html_start[] asm("_binary_static_logs_html_start");

void sendStatic(httpd_req_t* req, const char* contentType, const char* data) {
  HttpUtils::sendResponse(req, "200 OK", contentType, String(data));
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
                          getStatusJson().c_str());
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
  restart();
  return ESP_OK;
}

#if defined(EBUS_INTERNAL)
esp_err_t handleCommandsPage(httpd_req_t* req) {
  sendStatic(req, "text/html", commands_html_start);
  return ESP_OK;
}

esp_err_t handleCommands(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8",
                          store.getCommandsJson().c_str());
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
    HttpUtils::sendResponse(req, "200 OK", "text/html", String(bytes) + " bytes loaded");
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
    HttpUtils::sendResponse(req, "200 OK", "text/html", String(bytes) + " bytes saved");
  else if (bytes < 0)
    HttpUtils::sendResponse(req, "200 OK", "text/html", "Saving failed");
  else
    HttpUtils::sendResponse(req, "200 OK", "text/html", "No data saved");
  return ESP_OK;
}

esp_err_t handleCommandsWipe(httpd_req_t* req) {
  int64_t bytes = store.wipeCommands();
  if (bytes > 0)
    HttpUtils::sendResponse(req, "200 OK", "text/html", String(bytes) + " bytes wiped");
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
               store.getValuesJson().c_str());
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
        HttpUtils::sendResponse(req, "403 Forbidden", "text/html",
                     String("Invalid value for key '") + key.c_str());
      }
    } else {
      HttpUtils::sendResponse(req, "403 Forbidden", "text/html",
                   String("Key '") + key.c_str() + "' not found");
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
               deviceManager.getDevicesJson().c_str());
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
               schedule.getCounterJson().c_str());
  return ESP_OK;
}

esp_err_t handleStatisticsTiming(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8",
               schedule.getTimingJson().c_str());
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
  if (isCaptivePortalActive()) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Location", "/config");
    httpd_resp_send(req, "", 0);
    return ESP_OK;
  }

  HttpUtils::sendResponse(req, "404 Not Found", "text/plain", "Not found");
  return ESP_OK;
}

void registerUri(const char* uri, httpd_method_t method,
                 esp_err_t (*handler)(httpd_req_t*)) {
  HttpUtils::registerRoute(configServer, uri, method, handler);
}

}  // namespace

httpd_handle_t GetHttpServer() { return configServer; }

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

  registerUri("/common.css", HTTP_GET, handleCommonCss);
  registerUri("/common.js", HTTP_GET, handleCommonJs);
  registerUri("/", HTTP_GET, handleRoot);
  registerUri("/config", HTTP_GET, handleConfigPage);
  registerUri("/config2", HTTP_GET, handleConfigPage);
  registerUri("/status", HTTP_GET, handleStatusPage);
  registerUri("/api/v1/status", HTTP_GET, handleStatusApi);
  registerUri("/upgrade", HTTP_GET, handleUpgradePage);

#if defined(EBUS_INTERNAL)
  registerUri("/commands", HTTP_GET, handleCommandsPage);
  registerUri("/api/v1/commands", HTTP_GET, handleCommands);
  registerUri("/api/v1/commands/evaluate", HTTP_POST, handleCommandsEvaluate);
  registerUri("/api/v1/commands/insert", HTTP_POST, handleCommandsInsert);
  registerUri("/api/v1/commands/remove", HTTP_POST, handleCommandsRemove);
  registerUri("/api/v1/commands/load", HTTP_POST, handleCommandsLoad);
  registerUri("/api/v1/commands/save", HTTP_POST, handleCommandsSave);
  registerUri("/api/v1/commands/wipe", HTTP_POST, handleCommandsWipe);

  registerUri("/values", HTTP_GET, handleValuesPage);
  registerUri("/api/v1/values", HTTP_GET, handleValues);
  registerUri("/api/v1/values/write", HTTP_POST, handleValuesWrite);
  registerUri("/api/v1/values/read", HTTP_POST, handleValuesRead);

  registerUri("/devices", HTTP_GET, handleDevicesPage);
  registerUri("/api/v1/devices", HTTP_GET, handleDevices);
  registerUri("/api/v1/devices/scan", HTTP_POST, handleDevicesScan);
  registerUri("/api/v1/devices/scan/full", HTTP_POST, handleDevicesScanFull);
  registerUri("/api/v1/devices/scan/vendor", HTTP_POST, handleDevicesScanVendor);

  registerUri("/statistics", HTTP_GET, handleStatisticsPage);
  registerUri("/api/v1/statistics/counter", HTTP_GET, handleStatisticsCounter);
  registerUri("/api/v1/statistics/timing", HTTP_GET, handleStatisticsTiming);
  registerUri("/api/v1/statistics/reset", HTTP_POST, handleStatisticsReset);

  registerUri("/logs", HTTP_GET, handleLogsPage);
  registerUri("/api/v1/logs", HTTP_GET, handleLogs);
#endif

  registerUri("/restart", HTTP_POST, handleRestart);
}

void SetupHttpFallbackHandlers() {
  if (configServer == nullptr || fallbackHandlersRegistered) return;
  registerUri("/*", HTTP_GET, handleNotFound);
  registerUri("/*", HTTP_POST, handleNotFound);
  fallbackHandlersRegistered = true;
}
