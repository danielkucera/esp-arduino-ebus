#include "http.hpp"

#include "log.hpp"
#include "main.hpp"
#include "mqttha.hpp"
#include "schedule.hpp"
#include "store.hpp"

WebServer configServer(80);

void handleCommonCSS() {
  extern const char common_css_start[] asm("_binary_static_common_css_start");
  configServer.send(200, "text/css", common_css_start);
};

void handleCommonJS() {
  extern const char common_js_start[] asm("_binary_static_common_js_start");
  configServer.send(200, "application/javascript", common_js_start);
};

void handleStatusPage() {
  extern const char status_html_start[] asm("_binary_static_status_html_start");
  configServer.send(200, "text/html", status_html_start);
}

void handleGetStatus() {
  configServer.send(200, "application/json;charset=utf-8",
                    getStatusJson().c_str());
}

#if defined(EBUS_INTERNAL)
void handleCommandsPage() {
  extern const char commands_html_start[] asm(
      "_binary_static_commands_html_start");
  configServer.send(200, "text/html", commands_html_start);
}

// void handleCommandsList() {
//   configServer.send(200, "application/json;charset=utf-8",
//                     store.getCommandsJson().c_str());
// }

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

void handleValuesPage() {
  extern const char values_html_start[] asm("_binary_static_values_html_start");
  configServer.send(200, "text/html", values_html_start);
}

void handleValues() {
  configServer.send(200, "application/json;charset=utf-8",
                    store.getValuesJson().c_str());
}

void handleDevicesPage() {
  extern const char devices_html_start[] asm(
      "_binary_static_devices_html_start");
  configServer.send(200, "text/html", devices_html_start);
}

void handleDevices() {
  configServer.send(200, "application/json;charset=utf-8",
                    schedule.getDevicesJson().c_str());
}

void handleScan() {
  schedule.handleScan();
  configServer.send(200, "text/html", "Scan initiated");
}

void handleScanFull() {
  schedule.handleScanFull();
  configServer.send(200, "text/html", "Full scan initiated");
}

void handleScanVendor() {
  schedule.handleScanVendor();
  configServer.send(200, "text/html", "Vendor scan initiated");
}

void handleStatisticPage() {
  extern const char statistic_html_start[] asm(
      "_binary_static_statistic_html_start");
  configServer.send(200, "text/html", statistic_html_start);
}

void handleGetCounter() {
  configServer.send(200, "application/json;charset=utf-8",
                    schedule.getCounterJson().c_str());
}

void handleGetTiming() {
  configServer.send(200, "application/json;charset=utf-8",
                    schedule.getTimingJson().c_str());
}

void handleResetStatistic() {
  schedule.resetCounter();
  schedule.resetTiming();
  configServer.send(200, "text/html", "Statistic reset");
}

void handleLog() {
  extern const char log_html_start[] asm("_binary_static_log_html_start");
  configServer.send(200, "text/html", log_html_start);
}

void handleLogData() { configServer.send(200, "text/plain", getLog()); }
#endif

void handleRoot() {
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal()) {
    // -- Captive portal request were already served.
    return;
  }

  extern const char root_html_start[] asm("_binary_static_root_html_start");
  configServer.send(200, "text/html", root_html_start);
}

void SetupHttpHandlers() {
  // -- Set up required URL handlers on the web server.
  configServer.on("/", [] { handleRoot(); });
  configServer.on("/status", [] { handleStatusPage(); });
  configServer.on("/api/v1/GetStatus", [] { handleGetStatus(); });
#if defined(EBUS_INTERNAL)
  configServer.on("/commands", [] { handleCommandsPage(); });
  // configServer.on("/api/v1/CommandsList", [] { handleCommandsList(); });
  configServer.on("/api/v1/CommandsDownload", [] { handleCommandsDownload(); });
  configServer.on("/api/v1/CommandsEvaluate", [] { handleCommandsEvaluate(); });
  configServer.on("/api/v1/CommandsInsert", [] { handleCommandsInsert(); });
  configServer.on("/api/v1/CommandsLoad", [] { handleCommandsLoad(); });
  configServer.on("/api/v1/CommandsSave", [] { handleCommandsSave(); });
  configServer.on("/api/v1/CommandsWipe", [] { handleCommandsWipe(); });
  configServer.on("/values", [] { handleValuesPage(); });
  configServer.on("/api/v1/GetValues", [] { handleValues(); });
  configServer.on("/devices", [] { handleDevicesPage(); });
  configServer.on("/api/v1/GetDevices", [] { handleDevices(); });
  configServer.on("/api/v1/Scan", [] { handleScan(); });
  configServer.on("/api/v1/ScanFull", [] { handleScanFull(); });
  configServer.on("/api/v1/ScanVendor", [] { handleScanVendor(); });
  configServer.on("/statistic", [] { handleStatisticPage(); });
  configServer.on("/api/v1/GetCounter", [] { handleGetCounter(); });
  configServer.on("/api/v1/GetTiming", [] { handleGetTiming(); });
  configServer.on("/api/v1/ResetStatistic", [] { handleResetStatistic(); });
  configServer.on("/log", [] { handleLog(); });
  configServer.on("/logdata", [] { handleLogData(); });
#endif
  configServer.on("/restart", [] { restart(); });
  configServer.on("/config", [] { iotWebConf.handleConfig(); });
  configServer.on("/common.css", [] { handleCommonCSS(); });
  configServer.on("/common.js", [] { handleCommonJS(); });
  configServer.onNotFound([]() { iotWebConf.handleNotFound(); });
}
