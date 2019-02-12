#include "ESP8266WiFi.h"
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "config.h"

#define MAX_SRV_CLIENTS 4
#define RXBUFFERSIZE 1024
#define STACK_PROTECTOR  512 // bytes
 
WiFiServer wifiServer(3333);
WiFiClient serverClients[MAX_SRV_CLIENTS];
 
void setup() {
 
  Serial.begin(2400);
  Serial.setRxBufferSize(RXBUFFERSIZE);
 
  delay(1000);
 
  WiFi.begin(WIFI_SSID, WIFI_PASS);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
  }
 
  wifiServer.begin();

  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
  if (WiFi.status() != WL_CONNECTED) {
    ESP.reset();
  }

  //check if there are any new clients
  if (wifiServer.hasClient()) {
    //find free/disconnected spot
    int i;
    for (i = 0; i < MAX_SRV_CLIENTS; i++)
      if (!serverClients[i]) { // equivalent to !serverClients[i].connected()
        serverClients[i] = wifiServer.available();
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
      }
    }
  }

}
