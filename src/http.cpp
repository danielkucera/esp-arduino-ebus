#include "http.hpp"

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
  configServer.send(200, "application/json", s);
}

void handleCommandsLoad() {
  int64_t bytes = store.loadCommands();
  if (bytes > 0)
    configServer.send(200, "text/html", "Successfully loaded");
  else if (bytes < 0)
    configServer.send(200, "text/html", "Loading failed");
  else
    configServer.send(200, "text/html", "No data loaded");

  if (mqttha.isEnabled()) mqttha.publishComponents();
}

void handleCommandsSave() {
  int64_t bytes = store.saveCommands();
  if (bytes > 0)
    configServer.send(200, "text/html", "Successfully saved");
  else if (bytes < 0)
    configServer.send(200, "text/html", "Saving failed");
  else
    configServer.send(200, "text/html", "No data saved");
}

void handleCommandsWipe() {
  int64_t bytes = store.wipeCommands();
  if (bytes > 0)
    configServer.send(200, "text/html", "Successfully wiped");
  else if (bytes < 0)
    configServer.send(200, "text/html", "Wiping failed");
  else
    configServer.send(200, "text/html", "No data wiped");
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
      for (JsonVariantConst command : commands)
        store.insertCommand(store.createCommand(command));
      if (mqttha.isEnabled()) mqttha.publishComponents();
      configServer.send(200, "text/html", "OK");
    } else {
      configServer.send(403, "text/html", "NO COMMANDS");
    }
  }
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

#define MAX_LOG_ENTRIES 35
String logBuffer[MAX_LOG_ENTRIES];
int logIndex = 0;
int logEntries = 0;

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[30];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, millis() % 1000);

  return String(buffer);
}

void addLog(String entry) {
  String timestampedEntry = getTimestamp() + " " + entry;

  logBuffer[logIndex] = timestampedEntry;

  logIndex = (logIndex + 1) % MAX_LOG_ENTRIES;

  if (logEntries < MAX_LOG_ENTRIES) logEntries++;
}

void handleMonitorData() {
  String response = "";

  for (int i = 0; i < logEntries; i++) {
    int index = (logIndex - logEntries + i + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    response += logBuffer[index] + "\n";
  }

  configServer.send(200, "text/plain", response);
}

#endif

void handleRoot() {
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal()) {
    // -- Captive portal request were already served.
    return;
  }
  // clang-format off
  String s;
  s += "<html><head><title>esp-eBus adapter</title>";
  s += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += R"(
    <script type='text/javascript'>
      function confirmAction(action) {
        return confirm("Are you sure you want to " + action + "?");
      }
    </script>
  </head><body>
  )";

  s += "<a href='/status'>Adapter status</a><br>";

#if defined(EBUS_INTERNAL)
  s += R"(
    <a href='/commands/list'>List commands</a><br>
    <a href='/commands/upload'>Upload commands</a><br>
    <a href='/commands/download'>Download commands</a><br>
    <a href='/commands/load' onclick="return confirmAction('load commands');">Load commands</a><br>
    <a href='/commands/save' onclick="return confirmAction('save commands');">Save commands</a><br>
    <a href='/commands/wipe' onclick="return confirmAction('wipe commands');">Wipe commands</a><br>
    <a href='/values'>Values</a><br>
    <a href='/scan' onclick="return confirmAction('scan');">Scan</a><br>
    <a href='/scanfull' onclick="return confirmAction('scan full');">Scan full</a><br>
    <a href='/scanvendor' onclick="return confirmAction('scan vendor');">Scan vendor</a><br>
    <a href='/participants'>Participants</a><br>
    <a href='/reset' onclick="return confirmAction('reset');">Reset statistic</a><br>
    <a href='/monitor'>Monitor</a><br>
  )";
#endif

  s += R"(
    <a href='/restart' onclick="return confirmAction('restart');">Restart</a><br>
    <a href='/config'>Configuration</a> - user: admin password: your configured AP mode password or default password<br>
    <a href='/firmware'>Firmware update</a><br><br>
    For more info see project page: <a href='https://github.com/danielkucera/esp-arduino-ebus'>https://github.com/danielkucera/esp-arduino-ebus</a>
  </body></html>
  )";
  // clang-format on

  configServer.send(200, "text/html", s);
}

#if defined(EBUS_INTERNAL)
void handleMonitor() {
  const char* monitorHtml = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>esp-eBus monitor</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 16px; }
        pre { background-color: #f4f4f4; padding: 10px; border-radius: 5px; }
    </style>
</head>
<body>
    <h1>Monitor</h1>
    <pre id="logContainer">Loading logs...</pre>
    <script>
        function fetchLogs() {
            fetch('/monitordata') 
                .then(response => {
                    if (!response.ok) throw new Error('Network response was not ok');
                    return response.text();
                })
                .then(data => { document.getElementById('logContainer').innerText = data; })
                .catch(error => { console.error('Fetch error:', error); });
        }
        setInterval(fetchLogs, 500);  // Fetch logs every 500 milliseconds
        fetchLogs();  // Initial fetch
    </script>
</body>
</html>
)rawliteral";

  configServer.send(200, "text/html", monitorHtml);
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
  configServer.on("/monitor", [] { handleMonitor(); });
  configServer.on("/monitordata", [] { handleMonitorData(); });
#endif
  configServer.on("/restart", [] { restart(); });
  configServer.on("/config", [] { iotWebConf.handleConfig(); });
  configServer.onNotFound([]() { iotWebConf.handleNotFound(); });
}
