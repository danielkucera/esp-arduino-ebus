#include "http.hpp"

#include "main.hpp"
#include "schedule.hpp"
#include "store.hpp"

WebServer configServer(80);

void handleStatus() { configServer.send(200, "text/plain", status_string()); }

void handleGetAdapter() {
  configServer.send(200, "application/json;charset=utf-8",
                    getAdapterJson().c_str());
}

void handleGetStatus() {
  configServer.send(200, "application/json;charset=utf-8",
                    getStatusJson().c_str());
}

#ifdef EBUS_INTERNAL
void handleCommandsList() {
  configServer.send(200, "application/json;charset=utf-8",
                    store.getCommandsJson().c_str());
}

void handleCommandsUpload() {
  configServer.send(200, "text/html", F(R"(<html>
  <head><title>esp-eBus adapter</title>
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
    JsonArray array = doc["commands"].as<JsonArray>();
    if (array != nullptr) {
      for (JsonVariant variant : array)
        store.insertCommand(store.createCommand(variant));
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

void handleParicipantsScanSeen() {
  schedule.handleScanSeen();
  configServer.send(200, "text/html", "Scan initiated");
}

void handleParicipantsScanFull() {
  schedule.handleScanFull();
  configServer.send(200, "text/html", "Scan full initiated");
}

void handleParicipantsList() {
  configServer.send(200, "application/json;charset=utf-8",
                    schedule.getParticipantsJson().c_str());
}

void handleGetCounters() {
  configServer.send(200, "application/json;charset=utf-8",
                    schedule.getCountersJson().c_str());
}
#endif

void handleRoot() {
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal()) {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<html><head><title>esp-eBus adapter</title>";
  s += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, "
       "user-scalable=no\"/>";
  s += "  <script type='text/javascript'>";
  s += "     function confirmAction(action){ return confirm(\"Are you sure you "
       "want to \" + action + \"?\"); }\n";
  s += "  </script>\n";
  s += "</head><body>";
  s += "<a href='/status'>Adapter status</a><br>";
#ifdef EBUS_INTERNAL
  s += "<a href='/commands/list'>List commands</a><br>";
  s += "<a href='/commands/upload'>Upload commands</a><br>";
  s += "<a href='/commands/download'>Download commands</a><br>";
  s += "<a href='/commands/load' onclick=\"return "
       "confirmAction('load commands');\">Load commands</a><br>";
  s += "<a href='/commands/save' onclick=\"return "
       "confirmAction('save commands');\">Save commands</a><br>";
  s += "<a href='/commands/wipe' onclick=\"return "
       "confirmAction('wipe commands');\">Wipe commands</a><br>";
  s += "<a href='/values'>Values</a><br>";
  s += "<a href='/participants/scan' onclick=\"return "
       "confirmAction('scan');\">Scan</a><br>";
  s += "<a href='/participants/scanfull' onclick=\"return "
       "confirmAction('scan full');\">Scan full</a><br>";
  s += "<a href='/participants/list'>Participants</a><br>";
#endif
  s += "<a href='/restart' onclick=\"return "
       "confirmAction('restart');\">Restart</a><br>";
  s += "<a href='/config'>Configuration</a> - user: admin password: your "
       "configured AP mode password or default password";
  s += "<br>";
  s += "<a href='/firmware'>Firmware update</a><br>";
  s += "<br>";
  s += "For more info see project page: <a "
       "href='https://github.com/danielkucera/esp-arduino-ebus'>https://"
       "github.com/danielkucera/esp-arduino-ebus</a>";
  s += "</body></html>";

  configServer.send(200, "text/html", s);
}

void SetupHttpHandlers() {
  // -- Set up required URL handlers on the web server.
  configServer.on("/", [] { handleRoot(); });
  configServer.on("/status", [] { handleStatus(); });
  configServer.on("/api/v1/GetAdapter", [] { handleGetAdapter(); });
  configServer.on("/api/v1/GetStatus", [] { handleGetStatus(); });
#ifdef EBUS_INTERNAL
  configServer.on("/commands/list", [] { handleCommandsList(); });
  configServer.on("/commands/download", [] { handleCommandsDownload(); });
  configServer.on("/commands/upload", [] { handleCommandsUpload(); });
  configServer.on("/commands/insert", [] { handleCommandsInsert(); });
  configServer.on("/commands/load", [] { handleCommandsLoad(); });
  configServer.on("/commands/save", [] { handleCommandsSave(); });
  configServer.on("/commands/wipe", [] { handleCommandsWipe(); });
  configServer.on("/values", [] { handleValues(); });
  configServer.on("/participants/scanseen",
                  [] { handleParicipantsScanSeen(); });
  configServer.on("/participants/scanfull",
                  [] { handleParicipantsScanFull(); });
  configServer.on("/participants/list", [] { handleParicipantsList(); });
  configServer.on("/api/v1/GetCounters", [] { handleGetCounters(); });
#endif
  configServer.on("/restart", [] { restart(); });
  configServer.on("/config", [] { iotWebConf.handleConfig(); });
  configServer.onNotFound([]() { iotWebConf.handleNotFound(); });
}
