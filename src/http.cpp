#include <WebServer.h>

#include "main.hpp"
#include "http.hpp"

WebServer configServer(80);

void handleStatus() { 
  configServer.send(200, "text/plain", status_string()); 
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
</body></html>
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
  
)") );
}

void handleCommandsDownload(){
  String s = "{ \"id\": \"insert\",\n   \"commands\": ";
    s += store.getCommandsJson().c_str();
    s += "\n}";
  configServer.send(200, "application/json", s );
}

void handleCommandsLoad() {
  int64_t bytes = store.loadCommands();
  if (bytes > 0)
    configServer.send(200, "text/html","Successfully loaded");
   else if (bytes < 0)
    configServer.send(200, "text/html","Loading failed");
  else
    configServer.send(200, "text/html","No data loaded");
}

void handleCommandsSave() {
  int64_t bytes = store.saveCommands();
  if (bytes > 0)
    configServer.send(200, "text/html","Successfully saved");
   else if (bytes < 0)
    configServer.send(200, "text/html","Saving failed");
  else
    configServer.send(200, "text/html","No data saved");
}

void handleCommandsWipe() {
  int64_t bytes = store.wipeCommands();
  if (bytes > 0)
    configServer.send(200, "text/html","Successfully wiped");
   else if (bytes < 0)
    configServer.send(200, "text/html","Wiping failed");
  else
    configServer.send(200, "text/html","No data wiped");
}

void handleCommandsInsert() {
  JsonDocument doc;
  String body = configServer.arg("plain"); 
 
  DeserializationError error = deserializeJson(doc, body);

  if (error)
    configServer.send(403, "text/html", "INVALID JSON");
  else
  {
    JsonArray array = doc["commands"].as<JsonArray>();
    if (array != nullptr) 
    {
      for (JsonVariant variant : array)
          store.insertCommand( store.createCommand(variant) );
      configServer.send(200, "text/html", "OK");
    }
    else
      configServer.send(403, "text/html", "NO COMMANDS");
  }
}

void handleValues() {
  configServer.send(200, "application/json;charset=utf-8",
                    store.getValuesJson().c_str());
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
  s += "</head><body>";
  s += "<a href='/status'>Adapter status</a><br>";

#ifdef EBUS_INTERNAL
  s += "<a href='/commands/list'>List commands</a><br>";
  s += "<a href='/commands/upload'>Upload commands</a><br>";
  s += "<a href='/commands/download'>Download commands</a><br>";
  s += "<a href='/commands/load'>Load commands</a><br>";
  s += "<a href='/commands/save'>Save commands</a><br>";
  s += "<a href='/commands/wipe'>Wipe commands</a><br>";
  s += "<a href='/values'>Values</a><br>";
  s += "<a href='/restart'>Restart</a><br>";
#endif

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

void HandleNotFound(){
    configServer.send(403, "text/html", "Page Not Found");
}

void SetupHttpHandlers()
{
  // -- Set up required URL handlers on the web server.
  configServer.on("/", [] { handleRoot(); });
  configServer.on("/status", [] { handleStatus(); });

#ifdef EBUS_INTERNAL
  configServer.on("/commands/list", [] { handleCommandsList(); });
  configServer.on("/commands/download", [] { handleCommandsDownload(); });
  configServer.on("/commands/upload", [] { handleCommandsUpload(); });
  configServer.on("/commands/insert", [] { handleCommandsInsert(); });
  configServer.on("/commands/load", [] { handleCommandsLoad(); });
  configServer.on("/commands/save", [] { handleCommandsSave(); });
  configServer.on("/commands/wipe", [] { handleCommandsWipe(); });
  configServer.on("/values", [] { handleValues(); });
#endif
  configServer.on("/restart", [] { restart(); });
  configServer.onNotFound([]() { iotWebConf.handleNotFound(); });
}