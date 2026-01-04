#include "http.hpp"

#include "log.hpp"
#include "main.hpp"
#include "mqttha.hpp"
#include "schedule.hpp"
#include "store.hpp"

WebServer configServer(80);

// Structure to hold endpoint info
struct Endpoint {
  String path;
  String method;
  String summary;
};

// Array to store all registered endpoints
Endpoint endpoints[] = {
    // status
    {"/api/v1/status", "get", "Get status information"},
#if defined(EBUS_INTERNAL)
    // commands
    {"/api/v1/commands/list", "get", "List commands from store"},
    {"/api/v1/commands/download", "get", "Download commands from store"},
    {"/api/v1/commands/evaluate", "get", "Evaluate commands from editor"},
    {"/api/v1/commands/insert", "get", "Insert commands to store"},
    {"/api/v1/commands/remove", "get", "Remove commands from store"},
    {"/api/v1/commands/load", "get", "Load commands from NVS"},
    {"/api/v1/commands/save", "get", "Save commands save to NVS"},
    {"/api/v1/commands/wipe", "get", "Wipe all commands from NVS"},
    // values
    {"/api/v1/values", "get", "List values of stored commands"},
    // devices
    {"/api/v1/devices", "get", "List scanned devices"},
    {"/api/v1/devices/scan", "get", "Scan of seen devices"},
    {"/api/v1/devices/scan/full", "get", "Scan of full bus"},
    {"/api/v1/devices/scan/vendor", "get", "Scan vendor specific"},
    // statistics
    {"/api/v1/statistics/counter", "get", "List counter values"},
    {"/api/v1/statistics/timing", "get", "List timing values"},
    {"/api/v1/statistics/reset", "get", "Reset counter and timing values"},
    // logs
    {"/api/v1/logs", "get", "Raw data of logging data"},
#endif
    // api
    {"/api/v1/api", "get", "This site"},
};

// Total number of endpoints
const int endpointCount = sizeof(endpoints) / sizeof(endpoints[0]);

String generateAPI() {
  String json = "{";
  json += "\"openapi\": \"3.0.0\",";
  json +=
      "\"info\": {\"title\": \"esp-ebus API Documentation\", \"version\": "
      "\"1.0.0\"},";
  json += "\"paths\": {";

  for (int i = 0; i < endpointCount; i++) {
    json += "\"" + endpoints[i].path + "\": {";
    json += "\"" + endpoints[i].method + "\": {";
    json += "\"summary\": \"" + endpoints[i].summary + "\",";
    json += "\"responses\": {\"200\": {\"description\": \"Success\"}}";
    json += "}},";  // Close method object
  }

  if (endpointCount > 0) json.remove(json.length() - 1);

  json += "}}";

  return json;
}

void handleAPIPage() {
  String openApiSpec = generateAPI();
  configServer.send(200, "application/json", openApiSpec);
}

// common
void handleCommonCSS() {
  extern const char common_css_start[] asm("_binary_static_common_css_start");
  configServer.send(200, "text/css", common_css_start);
};

void handleCommonJS() {
  extern const char common_js_start[] asm("_binary_static_common_js_start");
  configServer.send(200, "application/javascript", common_js_start);
};

// root
void handleRoot() {
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal()) {
    // -- Captive portal request were already served.
    return;
  }

  extern const char root_html_start[] asm("_binary_static_root_html_start");
  configServer.send(200, "text/html", root_html_start);
}

// status
void handleStatusPage() {
  extern const char status_html_start[] asm("_binary_static_status_html_start");
  configServer.send(200, "text/html", status_html_start);
}

void handleStatus() {
  configServer.send(200, "application/json;charset=utf-8",
                    getStatusJson().c_str());
}

#if defined(EBUS_INTERNAL)
// commands
void handleCommandsPage() {
  extern const char commands_html_start[] asm(
      "_binary_static_commands_html_start");
  configServer.send(200, "text/html", commands_html_start);
}

void handleCommandsList() {
  configServer.send(200, "application/json;charset=utf-8",
                    store.getCommandsJson().c_str());
}

void handleCommandsDownload() {
  String s = "{\"id\":\"insert\",\"commands\":";
  s += store.getCommandsJson().c_str();
  s += "}";
  configServer.sendHeader("Content-Disposition",
                          "attachment; filename=esp-ebus-commands.json");
  configServer.send(200, "application/json", s);
}

void handleCommandsEvaluate() {
  JsonDocument doc;
  String body = configServer.arg("plain");

  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    configServer.send(403, "text/html", "Json invalid");
  } else {
    JsonArrayConst commands = doc["commands"].as<JsonArrayConst>();
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
    JsonArrayConst commands = doc["commands"].as<JsonArrayConst>();
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
    if (!keys.isNull()) {
      for (JsonVariantConst key : keys) {
        const Command* cmd = store.findCommand(key.as<std::string>());
        if (cmd) store.removeCommand(key.as<std::string>());
      }
      configServer.send(200, "text/html", "Ok");
    } else if (store.getActiveCommands() + store.getPassiveCommands() > 0) {
      for (const Command* command : store.getCommands())
        store.removeCommand(command->key);
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
void handleValuesPage() {
  extern const char values_html_start[] asm("_binary_static_values_html_start");
  configServer.send(200, "text/html", values_html_start);
}

void handleValues() {
  configServer.send(200, "application/json;charset=utf-8",
                    store.getValuesJson().c_str());
}

// devices
void handleDevicesPage() {
  extern const char devices_html_start[] asm(
      "_binary_static_devices_html_start");
  configServer.send(200, "text/html", devices_html_start);
}

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
void handleStatisticsPage() {
  extern const char statistics_html_start[] asm(
      "_binary_static_statistics_html_start");
  configServer.send(200, "text/html", statistics_html_start);
}

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
void handleLogsPage() {
  extern const char logs_html_start[] asm("_binary_static_logs_html_start");
  configServer.send(200, "text/html", logs_html_start);
}

void handleLogs() { configServer.send(200, "text/plain", getLogs()); }
#endif

void SetupHttpHandlers() {
  // -- Set up required URL handlers on the web server.
  configServer.onNotFound([]() { iotWebConf.handleNotFound(); });

  // common
  configServer.on("/common.css", [] { handleCommonCSS(); });
  configServer.on("/common.js", [] { handleCommonJS(); });

  // root
  configServer.on("/", [] { handleRoot(); });

  // config
  configServer.on("/config", [] { iotWebConf.handleConfig(); });

  // status
  configServer.on("/status", [] { handleStatusPage(); });
  configServer.on("/api/v1/status", [] { handleStatus(); });

#if defined(EBUS_INTERNAL)
  // commands
  configServer.on("/commands", [] { handleCommandsPage(); });
  configServer.on("/api/v1/commands/list", [] { handleCommandsList(); });
  configServer.on("/api/v1/commands/download",
                  [] { handleCommandsDownload(); });
  configServer.on("/api/v1/commands/evaluate",
                  [] { handleCommandsEvaluate(); });
  configServer.on("/api/v1/commands/insert", [] { handleCommandsInsert(); });
  configServer.on("/api/v1/commands/remove", [] { handleCommandsRemove(); });
  configServer.on("/api/v1/commands/load", [] { handleCommandsLoad(); });
  configServer.on("/api/v1/commands/save", [] { handleCommandsSave(); });
  configServer.on("/api/v1/commands/wipe", [] { handleCommandsWipe(); });

  // values
  configServer.on("/values", [] { handleValuesPage(); });
  configServer.on("/api/v1/values", [] { handleValues(); });

  // devices
  configServer.on("/devices", [] { handleDevicesPage(); });
  configServer.on("/api/v1/devices", [] { handleDevices(); });
  configServer.on("/api/v1/devices/scan", [] { handleDevicesScan(); });
  configServer.on("/api/v1/devices/scan/full", [] { handleDevicesScanFull(); });
  configServer.on("/api/v1/devices/scan/vendor",
                  [] { handleDevicesScanVendor(); });

  // statistics
  configServer.on("/statistics", [] { handleStatisticsPage(); });
  configServer.on("/api/v1/statistics/counter",
                  [] { handleStatisticsCounter(); });
  configServer.on("/api/v1/statistics/timing",
                  [] { handleStatisticsTiming(); });
  configServer.on("/api/v1/statistics/reset", [] { handleStatisticsReset(); });

  // logs
  configServer.on("/logs", [] { handleLogsPage(); });
  configServer.on("/api/v1/logs", [] { handleLogs(); });

  // api
  configServer.on("/api", [] { handleAPIPage(); });
#endif

  // restart
  configServer.on("/restart", [] { restart(); });
}
