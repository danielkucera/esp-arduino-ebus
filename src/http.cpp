#include "http.hpp"

#include "Logger.hpp"
#include "MqttHA.hpp"
#include "Schedule.hpp"
#include "Store.hpp"
#include "main.hpp"

WebServer configServer(80);

extern const char common_css_start[] asm("_binary_static_common_css_start");
extern const char common_js_start[] asm("_binary_static_common_js_start");

extern const char root_html_start[] asm("_binary_static_root_html_start");
extern const char status_html_start[] asm("_binary_static_status_html_start");
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
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal()) return;  // already served
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
  JsonDocument doc;
  String body = configServer.arg("plain");

  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    configServer.send(403, "text/html", "Json invalid");
  } else {
    JsonArrayConst commands = doc.as<JsonArrayConst>();
    if (!commands.isNull()) {
      for (JsonVariantConst command : commands) {
        std::string evalError = store.evaluateCommand(command);
        if (!evalError.empty()) {
          configServer.send(403, "text/html", evalError.c_str());
          return;
        }
      }
      configServer.send(200, "text/html", "Ok");
    } else {
      configServer.send(403, "text/html", "No commands");
    }
  }
}

void handleCommandsInsert() {
  JsonDocument doc;
  String body = configServer.arg("plain");

  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    configServer.send(403, "text/html", "Json invalid");
  } else {
    JsonArrayConst commands = doc.as<JsonArrayConst>();
    if (!commands.isNull()) {
      for (JsonVariantConst command : commands) {
        std::string evalError = store.evaluateCommand(command);
        if (evalError.empty())
          store.insertCommand(store.createCommand(command));
        else
          configServer.send(403, "text/html", evalError.c_str());
      }
      if (mqttha.isEnabled()) mqttha.publishComponents();
      configServer.send(200, "text/html", "Ok");
    } else {
      configServer.send(403, "text/html", "No commands");
    }
  }
}

void handleCommandsRemove() {
  JsonDocument doc;
  String body = configServer.arg("plain");

  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    configServer.send(403, "text/html", "Json invalid");
  } else {
    JsonArrayConst keys = doc["keys"].as<JsonArrayConst>();
    if (keys.size() > 0) {
      for (JsonVariantConst key : keys) {
        const Command* cmd = store.findCommand(key.as<std::string>());
        if (cmd) {
          if (mqttha.isEnabled()) mqttha.publishComponent(cmd, true);
          store.removeCommand(key.as<std::string>());
        }
      }
      configServer.send(200, "text/html", "Ok");
    } else if (store.getActiveCommands() + store.getPassiveCommands() > 0) {
      for (const Command* cmd : store.getCommands()) {
        if (mqttha.isEnabled()) mqttha.publishComponent(cmd, true);
        store.removeCommand(cmd->key);
      }
      configServer.send(200, "text/html", "Ok");
    } else {
      configServer.send(403, "text/html", "No commands");
    }
  }
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
  JsonDocument doc;
  String body = configServer.arg("plain");

  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    configServer.send(403, "text/html", error.c_str());
  } else {
    std::string key = doc["key"].as<std::string>();
    Command* command = store.findCommand(key);
    if (command != nullptr) {
      std::vector<uint8_t> valueBytes;
      if (command->numeric) {
        double value = doc["value"].as<double>();
        valueBytes = getVectorFromDouble(command, value);
      } else {
        std::string value = doc["value"].as<std::string>();
        valueBytes = getVectorFromString(command, value);
      }
      if (valueBytes.size() > 0) {
        std::vector<uint8_t> writeCmd = command->write_cmd;
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
}

void handleValuesRead() {
  JsonDocument doc;
  String body = configServer.arg("plain");

  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    // Return MQTT-equivalent error payload: {"id":"read","status": "..."}
    JsonDocument errDoc;
    errDoc["id"] = "read";
    errDoc["status"] = error.c_str();
    std::string payload;
    serializeJson(errDoc, payload);
    configServer.send(200, "application/json;charset=utf-8", payload.c_str());
  } else {
    std::string key = doc["key"].as<std::string>();
    Command* command = store.findCommand(key);
    if (command != nullptr) {
      // Force immediate refresh by resetting last seen timestamp
      command->last = 0;
      // Do not return the old value immediately; confirm the read request
      JsonDocument resp;
      resp["id"] = "read";
      resp["status"] = "requested";
      std::string payload;
      serializeJson(resp, payload);
      configServer.send(200, "application/json;charset=utf-8", payload.c_str());
    } else {
      JsonDocument errDoc;
      errDoc["id"] = "read";
      errDoc["status"] = std::string("Key '") + key + "' not found";
      std::string payload;
      serializeJson(errDoc, payload);
      configServer.send(200, "application/json;charset=utf-8", payload.c_str());
    }
  }
}

// devices
void handleDevices() {
  configServer.send(200, "application/json;charset=utf-8",
                    schedule.getDevicesJson().c_str());
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
  schedule.resetCounter();
  schedule.resetTiming();
  configServer.send(200, "text/html", "Statistics reset");
}

// logs
void handleLogs() { configServer.send(200, "text/plain", logger.getLogs()); }
#endif

void SetupHttpHandlers() {
  // -- Set up required URL handlers on the web server.
  configServer.onNotFound([]() { iotWebConf.handleNotFound(); });

  // common
  configServer.on("/common.css",
                  []() { handleStatic("text/css", common_css_start); });
  configServer.on("/common.js", []() {
    handleStatic("application/javascript", common_js_start);
  });

  // root
  configServer.on("/", [] { handleRoot(); });

  // config
  configServer.on("/config", [] { iotWebConf.handleConfig(); });

  // status
  configServer.on("/status",
                  []() { handleStatic("text/html", status_html_start); });
  configServer.on("/api/v1/status", [] { handleStatus(); });

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
  configServer.on("/restart", [] { restart(); });
}
