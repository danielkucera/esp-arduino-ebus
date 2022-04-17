#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#ifdef ESP32
#include <esp_task_wdt.h>
#endif

#define MAX_SRV_CLIENTS 4
#define RXBUFFERSIZE 1024
#define STACK_PROTECTOR  512 // bytes
#define HOSTNAME "esp-eBus"
 
WiFiServer wifiServer(3333);
WiFiServer statusServer(5555);
WiFiClient serverClients[MAX_SRV_CLIENTS];

unsigned long last_comms;

void wdt_start() {
#ifdef ESP32
  esp_task_wdt_init(6, true);
#elif defined(ESP8266)
  ESP.wdtDisable();
#endif
}

void wdt_feed() {
#ifdef ESP32
  esp_task_wdt_reset();
#elif defined(ESP8266)
  ESP.wdtFeed();
#else
#error UNKNOWN PLATFORM
#endif
}
 
void setup() {
  WiFiManager wifiManager;

  //wifiManager.resetSettings();

  wifiManager.setHostname(HOSTNAME);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.autoConnect(HOSTNAME);

  Serial.begin(2400);
  Serial.setRxBufferSize(RXBUFFERSIZE);
 
  wifiServer.begin();
  statusServer.begin();

  ArduinoOTA.begin();

  wdt_start();

  last_comms = millis();
}

void loop() {
  ArduinoOTA.handle();

  wdt_feed();

  if (WiFi.status() != WL_CONNECTED) {
    ESP.restart();
  }

  if (millis() > last_comms + 200*1000 ) {
    ESP.restart();
  }

  if (statusServer.hasClient()) {
    WiFiClient client = statusServer.available();
    if (client.availableForWrite() >= 1){
      client.write(String(millis()).c_str());
      client.flush();
      client.stop();
    }
  }

  //check if there are any new clients
  if (wifiServer.hasClient()) {
    //find free/disconnected spot
    int i;
    for (i = 0; i < MAX_SRV_CLIENTS; i++)
      if (!serverClients[i]) { // equivalent to !serverClients[i].connected()
        serverClients[i] = wifiServer.available();
        serverClients[i].setNoDelay(true);
        break;
      }

    //no free/disconnected spot so reject
    if (i == MAX_SRV_CLIENTS) {
      wifiServer.available().println("busy");
      // hints: server.available() is a WiFiClient with short-term scope
      // when out of scope, a WiFiClient will
      // - flush() - all data will be sent
      // - stop() - automatically too
    }
  }

  //check TCP clients for data
  for (int i = 0; i < MAX_SRV_CLIENTS; i++){
    while (serverClients[i].available() && Serial.availableForWrite() > 0) {
      // working char by char is not very efficient
      Serial.write(serverClients[i].read());
    }
  }

  //check UART for data
  size_t len = Serial.available();
  if (len) {
    byte B = Serial.read();
    // push UART data to all connected telnet clients
    for (int i = 0; i < MAX_SRV_CLIENTS; i++){
      // if client.availableForWrite() was 0 (congested)
      // and increased since then,
      // ensure write space is sufficient:
      if (serverClients[i].availableForWrite() >= 1) {
        serverClients[i].write(B);
        last_comms = millis();
      }
    }
  }

}
