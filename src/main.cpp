#include "ESP8266WiFi.h"
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
 
const char* ssid = "yourNetworkName";
const char* password =  "yourNetworkPass";
 
WiFiServer wifiServer(3333);
 
void setup() {
 
  Serial.begin(2400);
 
  delay(1000);
 
  WiFi.begin(ssid, password);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    //Serial.println("Connecting..");
  }
 
  //Serial.print("Connected to WiFi. IP:");
  //Serial.println(WiFi.localIP());
 
  wifiServer.begin();

  ArduinoOTA.begin();
}

int echo_read_byte(WiFiClient client){
  int B;
  int waited = 0;

  // wait until data available
  while (!client.available()){
    if (waited++ > 30){
      return -1;
    }
    delay(1);
  }

  B = client.read(); // read byte
  client.write(B); // echo back
  return B;
}

/* 
  Reads and echoes data from client until full request is received.
  Then waits for 0xaa on bus, sends the request and releases execution.
*/
void processRequest(WiFiClient client){
  char request[255];
  int i, B;
  int len = 6;

  // read full request
  for (i=0; i < len; i++){
    B = echo_read_byte(client);
    if (B < 0) {
      return;
    }
    request[i] = B;
    if (i == 4) {
      len = 6 + B;
    }

  }

  int b1, b2;

  // wait until 0xaa on bus
  while (true){
    b1 = Serial.read();
    if ((b1 = -1) && (b2 = 0xaa)){
      break;
    }
    b2 = b1;
  }

  Serial.write((uint8_t*)request,len);
  Serial.readBytes(request, len);

}

int B;

void loop() {
  ArduinoOTA.handle();
 
  WiFiClient client = wifiServer.available();
 
  if (client) {
 
    while (client.connected()) {
 
      if (client.available()>0) {
        // process client request here
        processRequest(client);

      } else {
        // send data from serial to client
        if (Serial.available()) {
          B = Serial.read();
          client.write(B);
        }

      }

    }
 
    client.stop();
    //Serial.println("Client disconnected");
 
  }
}