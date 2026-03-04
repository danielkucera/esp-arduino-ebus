#include "http.hpp"

#include "DeviceManager.hpp"
#include "Logger.hpp"
#include "MqttHA.hpp"
#include "Schedule.hpp"
#include "Store.hpp"
#include "main.hpp"
#include <cJSON.h>

WebServer configServer(80);

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

// static
void handleStatic(const char* contentType, const char* data) {
  configServer.send(200, contentType, data);
}

// root
void handleRoot() {
  handleStatic("text/html", root_html_start);
}

void handleStatus() {
  configServer.send(200, "application/json;charset=utf-8",
                    getStatusJson().c_str());
}

#if defined(EBUS_INTERNAL)
// commands
void handleCommands() {
  configServer.send(200, "application/json;charset=utf-8",
                    store.getCommandsJson().c_str());
}

void handleCommandsEvaluate() {
  String body = configServer.arg("plain");
  cJSON* doc = cJSON_Parse(body.c_str());
  if (!cJSON_IsArray(doc)) {
    configServer.send(403, "text/html", "Json invalid");
  } else {
    cJSON* command = nullptr;
    cJSON_ArrayForEach(command, doc) {
      std::string evalError = Command::evaluate(command);
      if (!evalError.empty()) {
        cJSON_Delete(doc);
        configServer.send(403, "text/html", evalError.c_str());
        return;
      }
    }
    configServer.send(200, "text/html", "Ok");
  }
  if (doc) cJSON_Delete(doc);
}

void handleCommandsInsert() {
  String body = configServer.arg("plain");
  cJSON* doc = cJSON_Parse(body.c_str());
  if (!cJSON_IsArray(doc)) {
    configServer.send(403, "text/html", "Json invalid");
  } else {
    cJSON* command = nullptr;
    cJSON_ArrayForEach(command, doc) {
      std::string evalError = Command::evaluate(command);
      if (evalError.empty())
        store.insertCommand(Command::fromJson(command));
      else {
        cJSON_Delete(doc);
        configServer.send(403, "text/html", evalError.c_str());
        return;
      }
    }
    if (mqttha.isEnabled()) mqttha.publishComponents();
    configServer.send(200, "text/html", "Ok");
  }
  if (doc) cJSON_Delete(doc);
}

void handleCommandsRemove() {
  String body = configServer.arg("plain");
  cJSON* doc = cJSON_Parse(body.c_str());
  if (!cJSON_IsObject(doc)) {
    configServer.send(403, "text/html", "Json invalid");
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
      configServer.send(200, "text/html", "Ok");
    } else if (store.getActiveCommands() + store.getPassiveCommands() > 0) {
      for (const Command* cmd : store.getCommands()) {
        if (mqttha.isEnabled()) mqttha.publishComponent(cmd, true);
        store.removeCommand(cmd->getKey());
      }
      configServer.send(200, "text/html", "Ok");
    } else {
      configServer.send(403, "text/html", "No commands");
    }
  }
  if (doc) cJSON_Delete(doc);
}

void handleCommandsLoad() {
  int64_t bytes = store.loadCommands();
  if (bytes > 0)
    configServer.send(200, "text/html", String(bytes) + " bytes loaded");
  else if (bytes < 0)
    configServer.send(200, "text/html", "Loading failed");
  else
    configServer.send(200, "text/html", "No data loaded");

  if (mqttha.isEnabled()) mqttha.publishComponents();
}

void handleCommandsSave() {
  int64_t bytes = store.saveCommands();
  if (bytes > 0)
    configServer.send(200, "text/html", String(bytes) + " bytes saved");
  else if (bytes < 0)
    configServer.send(200, "text/html", "Saving failed");
  else
    configServer.send(200, "text/html", "No data saved");
}

void handleCommandsWipe() {
  int64_t bytes = store.wipeCommands();
  if (bytes > 0)
    configServer.send(200, "text/html", String(bytes) + " bytes wiped");
  else if (bytes < 0)
    configServer.send(200, "text/html", "Wiping failed");
  else
    configServer.send(200, "text/html", "No data wiped");
}

// values
void handleValues() {
  configServer.send(200, "application/json;charset=utf-8",
                    store.getValuesJson().c_str());
}

void handleValuesWrite() {
  String body = configServer.arg("plain");
  cJSON* doc = cJSON_Parse(body.c_str());
  if (!cJSON_IsObject(doc)) {
    configServer.send(403, "text/html", "Json invalid");
  } else {
    cJSON* keyNode = cJSON_GetObjectItemCaseSensitive(doc, "key");
    std::string key =
        (cJSON_IsString(keyNode) && keyNode->valuestring != nullptr)
            ? keyNode->valuestring
            : "";
    Command* command = store.findCommand(key);
    if (command != nullptr) {
      std::vector<uint8_t> valueBytes = command->getVectorFromJson(doc);
      if (valueBytes.size() > 0) {
        std::vector<uint8_t> writeCmd = command->getWriteCmd();
        writeCmd.insert(writeCmd.end(), valueBytes.begin(), valueBytes.end());
        schedule.handleWrite(writeCmd);
        configServer.send(200, "text/html", "Ok");
      } else {
        configServer.send(403, "text/html",
                          String("Invalid value for key '") + key.c_str());
      }
    } else {
      configServer.send(403, "text/html",
                        String("Key '") + key.c_str() + "' not found");
    }
  }
  if (doc) cJSON_Delete(doc);
}

void handleValuesRead() {
  String body = configServer.arg("plain");
  cJSON* doc = cJSON_Parse(body.c_str());
  if (!cJSON_IsObject(doc)) {
    cJSON* errDoc = cJSON_CreateObject();
    cJSON_AddStringToObject(errDoc, "id", "read");
    cJSON_AddStringToObject(errDoc, "status", "invalid json payload");
    char* printed = cJSON_PrintUnformatted(errDoc);
    std::string payload = printed != nullptr ? printed : "{}";
    if (printed != nullptr) cJSON_free(printed);
    cJSON_Delete(errDoc);
    configServer.send(200, "application/json;charset=utf-8", payload.c_str());
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
      configServer.send(200, "application/json;charset=utf-8", payload.c_str());
    } else {
      cJSON* errDoc = cJSON_CreateObject();
      cJSON_AddStringToObject(errDoc, "id", "read");
      cJSON_AddStringToObject(errDoc, "status",
                              (std::string("Key '") + key + "' not found")
                                  .c_str());
      char* printed = cJSON_PrintUnformatted(errDoc);
      std::string payload = printed != nullptr ? printed : "{}";
      if (printed != nullptr) cJSON_free(printed);
      cJSON_Delete(errDoc);
      configServer.send(200, "application/json;charset=utf-8", payload.c_str());
    }
  }
  if (doc) cJSON_Delete(doc);
}

// devices
void handleDevices() {
  configServer.send(200, "application/json;charset=utf-8",
                    deviceManager.getDevicesJson().c_str());
}

void handleDevicesScan() {
  schedule.handleScan();
  configServer.send(200, "text/html", "Scan initiated");
}

void handleDevicesScanFull() {
  schedule.handleScanFull();
  configServer.send(200, "text/html", "Full scan initiated");
}

void handleDevicesScanVendor() {
  schedule.handleScanVendor();
  configServer.send(200, "text/html", "Vendor scan initiated");
}

// statistics
void handleStatisticsCounter() {
  configServer.send(200, "application/json;charset=utf-8",
                    schedule.getCounterJson().c_str());
}

void handleStatisticsTiming() {
  configServer.send(200, "application/json;charset=utf-8",
                    schedule.getTimingJson().c_str());
}

void handleStatisticsReset() {
  deviceManager.resetAddresses();
  schedule.resetCounter();
  schedule.resetTiming();
  configServer.send(200, "text/html", "Statistics reset");
}

// logs
void handleLogs() { configServer.send(200, "text/plain", logger.getLogs()); }
#endif

void SetupHttpHandlers() {
  // -- Set up required URL handlers on the web server.
  configServer.onNotFound([]() {
    if (isCaptivePortalActive()) {
      configServer.sendHeader("Location", "/config", true);
      configServer.send(302, "text/plain", "");
      return;
    }
    configServer.send(404, "text/plain", "Not found");
  });

  // common
  configServer.on("/common.css",
                  []() { handleStatic("text/css", common_css_start); });
  configServer.on("/common.js", []() {
    handleStatic("application/javascript", common_js_start);
  });

  // root
  configServer.on("/", [] { handleRoot(); });

  // config
  configServer.on("/config",
                  []() { handleStatic("text/html", config_html_start); });
  configServer.on("/config2",
                  []() { handleStatic("text/html", config_html_start); });

  // status
  configServer.on("/status",
                  []() { handleStatic("text/html", status_html_start); });
  configServer.on("/api/v1/status", [] { handleStatus(); });
  configServer.on("/upgrade",
                  []() { handleStatic("text/html", upgrade_html_start); });

#if defined(EBUS_INTERNAL)
  // commands
  configServer.on("/commands",
                  []() { handleStatic("text/html", commands_html_start); });
  configServer.on("/api/v1/commands", [] { handleCommands(); });
  configServer.on("/api/v1/commands/evaluate",
                  [] { handleCommandsEvaluate(); });
  configServer.on("/api/v1/commands/insert", [] { handleCommandsInsert(); });
  configServer.on("/api/v1/commands/remove", [] { handleCommandsRemove(); });
  configServer.on("/api/v1/commands/load", [] { handleCommandsLoad(); });
  configServer.on("/api/v1/commands/save", [] { handleCommandsSave(); });
  configServer.on("/api/v1/commands/wipe", [] { handleCommandsWipe(); });

  // values
  configServer.on("/values",
                  []() { handleStatic("text/html", values_html_start); });
  configServer.on("/api/v1/values", [] { handleValues(); });
  configServer.on("/api/v1/values/write", [] { handleValuesWrite(); });
  configServer.on("/api/v1/values/read", [] { handleValuesRead(); });

  // devices
  configServer.on("/devices",
                  []() { handleStatic("text/html", devices_html_start); });
  configServer.on("/api/v1/devices", [] { handleDevices(); });
  configServer.on("/api/v1/devices/scan", [] { handleDevicesScan(); });
  configServer.on("/api/v1/devices/scan/full", [] { handleDevicesScanFull(); });
  configServer.on("/api/v1/devices/scan/vendor",
                  [] { handleDevicesScanVendor(); });

  // statistics
  configServer.on("/statistics",
                  []() { handleStatic("text/html", statistics_html_start); });
  configServer.on("/api/v1/statistics/counter",
                  [] { handleStatisticsCounter(); });
  configServer.on("/api/v1/statistics/timing",
                  [] { handleStatisticsTiming(); });
  configServer.on("/api/v1/statistics/reset", [] { handleStatisticsReset(); });

  // logs
  configServer.on("/logs",
                  []() { handleStatic("text/html", logs_html_start); });
  configServer.on("/api/v1/logs", [] { handleLogs(); });
#endif

  // restart
  configServer.on("/restart", [] {
    configServer.send(200, "text/html", "Restarting...");
    configServer.client().flush(); // ensure response is sent before restarting
    configServer.client().stop(); // close the connection
    restart();
  });
}
