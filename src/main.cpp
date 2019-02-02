#include "ESP8266WiFi.h"
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "config.h"
 
const char* ssid = WIFI_SSID;
const char* password =  WIFI_PASS;
 
WiFiServer wifiServer(3333);
 
void setup() {
 
  Serial.begin(2400);
 
  delay(1000);
 
  WiFi.begin(ssid, password);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
  }
 
  wifiServer.begin();

  ArduinoOTA.begin();
}

int B;

WiFiClient client;

void loop() {
  ArduinoOTA.handle();

  if (wifiServer.hasClient()){
    client = wifiServer.available();
    client.setNoDelay(true);
  }

  if (client && client.connected()) {
 
    if (client.available()>0) {
      B = client.read();
      Serial.write(B);

    } else if (Serial.available()) {
      B = Serial.read();
      client.write(B);
    }

  }

}