#include "http.hpp"

#include "log.hpp"
#include "main.hpp"
#include "mqttha.hpp"
#include "schedule.hpp"
#include "store.hpp"

WebServer configServer(80);

void handleStatus() { configServer.send(200, "text/plain", status_string()); }

void handleGetStatus() {
  configServer.send(200, "application/json;charset=utf-8",
                    getStatusJson().c_str());
}

#if defined(EBUS_INTERNAL)
void handleCommandsList() {
  configServer.send(200, "application/json;charset=utf-8",
                    store.getCommandsJson().c_str());
}

void handleCommandsUpload() {
  configServer.send(200, "text/html", F(R"(<html>
  <head><title>esp-eBus upload</title>
  <meta name='viewport' content='width=device-width, initial-scale=1, user-scalable=no'>
  </head><body>
  <h3>Upload Commands</h3>
  <input type='file' id='file-upload'>
  <script type='text/javascript'>
  window.onload = function(event) {
    document.getElementById('file-upload').addEventListener('change', handleFileSelect, false);
  }
  function handleFileSelect(event) {
    var fileReader = new FileReader();
    fileReader.onload = function(event) {
      try {
        var commands=JSON.parse(event.target.result);
        var headers = new Headers();
        headers.append('Accept', 'text/plain');
        headers.append('Content-Type', 'text/plain');
        fetch('/commands/insert', { method: 'POST', headers: headers, body: JSON.stringify(commands) } )
        .then( response => {  alert(response.ok?"Succesfully loaded":"Something went wrong"); } );
      } catch (error) { 
        alert('Invalid JSON Config file'); 
      }
    }
    var file = event.target.files[0];
    fileReader.readAsText(file);
  }
  </script>
  </body></html>
  )"));
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
    configServer.send(403, "text/html", "INVALID JSON");
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
      configServer.send(200, "text/html", "OK");
    } else {
      configServer.send(403, "text/html", "NO COMMANDS");
    }
  }
}

void handleCommandsInsert() {
  JsonDocument doc;
  String body = configServer.arg("plain");

  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    configServer.send(403, "text/html", "INVALID JSON");
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
      configServer.send(200, "text/html", "OK");
    } else {
      configServer.send(403, "text/html", "NO COMMANDS");
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

void handleValues() {
  configServer.send(200, "application/json;charset=utf-8",
                    store.getValuesJson().c_str());
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

void handleParticipants() {
  configServer.send(200, "application/json;charset=utf-8",
                    schedule.getParticipantsJson().c_str());
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

#if defined(EBUS_INTERNAL)
void handleLog() {
  extern const char log_html_start[] asm("_binary_static_log_html_start");
  configServer.send(200, "text/html", log_html_start);
}
#endif

void SetupHttpHandlers() {
  // -- Set up required URL handlers on the web server.
  configServer.on("/", [] { handleRoot(); });
  configServer.on("/status", [] { handleStatus(); });
  configServer.on("/api/v1/GetStatus", [] { handleGetStatus(); });
#if defined(EBUS_INTERNAL)
  configServer.on("/commands/list", [] { handleCommandsList(); });
  configServer.on("/commands/download", [] { handleCommandsDownload(); });
  configServer.on("/commands/upload", [] { handleCommandsUpload(); });
  configServer.on("/commands/evaluate", [] { handleCommandsEvaluate(); });
  configServer.on("/commands/insert", [] { handleCommandsInsert(); });
  configServer.on("/commands/load", [] { handleCommandsLoad(); });
  configServer.on("/commands/save", [] { handleCommandsSave(); });
  configServer.on("/commands/wipe", [] { handleCommandsWipe(); });
  configServer.on("/values", [] { handleValues(); });
  configServer.on("/scan", [] { handleScan(); });
  configServer.on("/scanfull", [] { handleScanFull(); });
  configServer.on("/scanvendor", [] { handleScanVendor(); });
  configServer.on("/participants", [] { handleParticipants(); });
  configServer.on("/api/v1/GetCounter", [] { handleGetCounter(); });
  configServer.on("/api/v1/GetTiming", [] { handleGetTiming(); });
  configServer.on("/reset", [] { handleResetStatistic(); });
  configServer.on("/log", [] { handleLog(); });
  configServer.on("/logdata", [] { handleLogData(); });
#endif
  configServer.on("/restart", [] { restart(); });
  configServer.on("/config", [] { iotWebConf.handleConfig(); });
  configServer.onNotFound([]() { iotWebConf.handleNotFound(); });
}
