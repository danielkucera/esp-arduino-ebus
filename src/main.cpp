#include <ArduinoOTA.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include "main.hpp"
#include <Ebus.h>

#ifdef ESP32
  #include <esp_task_wdt.h>
  #include <ESPmDNS.h>
#else
  #include <ESP8266mDNS.h>
  #include <ESP8266TrueRandom.h>
#endif

WiFiServer wifiServer(3333);
WiFiServer wifiServerRO(3334);
WiFiServer wifiServerEnh(3335);
WiFiServer wifiServerMSG(8888);
WiFiServer statusServer(5555);
WiFiClient serverClients[MAX_SRV_CLIENTS];
WiFiClient serverClientsRO[MAX_SRV_CLIENTS];
WiFiClient enhClients[MAX_SRV_CLIENTS];
WiFiClient serverClientsMSG[MAX_SRV_CLIENTS];

unsigned long last_comms;

ebus::Ebus bus;

int random_ch(){
#ifdef ESP32
  return 6;
#elif defined(ESP8266)
  return ESP8266TrueRandom.random(1, 13);
#endif
}

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

inline void disableTX() {
    digitalWrite(TX_DISABLE_PIN, HIGH);
}

inline void enableTX() {
    digitalWrite(TX_DISABLE_PIN, LOW);
}

void reset(){
  disableTX();
  pinMode(TX_DISABLE_PIN, INPUT_PULLUP);
  ESP.restart();
}

void reset_config() {
  WiFiManager wifiManager(Serial1);  // Send debug on Serial1
  wifiManager.resetSettings();
  reset();
}
 
void setup() {

  Serial.setRxBufferSize(RXBUFFERSIZE);

#ifdef ESP32
  Serial1.begin(115200, SERIAL_8N1, 8, 10);
  Serial.begin(2400, SERIAL_8N1, 21, 20);
#elif defined(ESP8266)
  Serial1.begin(115200);
  Serial.begin(2400);
#endif

  Serial1.setDebugOutput(true);

  // check if RESET_PIN being hold low and reset
  pinMode(RESET_PIN, INPUT_PULLUP);
  unsigned long resetStart = millis();
  while(digitalRead(RESET_PIN) == 0){
    if (millis() > resetStart + RESET_MS){
      reset_config();
    }
  }

  disableTX();
  pinMode(TX_DISABLE_PIN, OUTPUT);

  WiFi.enableAP(false);
  WiFi.begin();

  WiFiManager wifiManager(Serial1);

  wifiManager.setHostname(HOSTNAME);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setWiFiAPChannel(random_ch());
  wifiManager.autoConnect(HOSTNAME);
 
  wifiServer.begin();
  wifiServerRO.begin();
  wifiServerEnh.begin();
  wifiServerMSG.begin();
  statusServer.begin();

  ArduinoOTA.begin();

  MDNS.end();
  MDNS.begin(HOSTNAME);

  wdt_start();

  last_comms = millis();
}


bool handleStatusServerRequests() {
  if (!statusServer.hasClient())
    return false;

  WiFiClient client = statusServer.available();

  if (client.availableForWrite() >= AVAILABLE_THRESHOLD) {
    client.printf("uptime: %ld ms\n", millis());
    client.printf("rssi: %d dBm\n", WiFi.RSSI());
    client.flush();
    client.stop();
  }
  return true;
}


void loop() {
  ArduinoOTA.handle();

#ifdef ESP8266
  MDNS.update();
#endif

  wdt_feed();

  if (WiFi.status() != WL_CONNECTED) {
    reset();
  }

  if (millis() > last_comms + 200*1000 ) {
    reset();
  }

  // Check if new client on the status server
  handleStatusServerRequests();

  // Check if there are any new clients on the eBUS servers
  if (handleNewClient(wifiServer, serverClients)){
    enableTX();
  }
  handleNewClient(wifiServerRO, serverClientsRO);
  handleNewClient(wifiServerEnh, enhClients);

  //check clients for data
  for (int i = 0; i < MAX_SRV_CLIENTS; i++){
    handleClient(&serverClients[i]);
    handleEnhClient(&enhClients[i]);
  }

  //check UART for data
  if (size_t len = Serial.available()) {
    byte B = Serial.read();

    // push UART data to all connected telnet clients
    for (int i = 0; i < MAX_SRV_CLIENTS; i++){
      if (pushClient(&serverClients[i], B)){
        last_comms = millis();
      }
      if (pushClient(&serverClientsRO[i], B)){
        last_comms = millis();
      }
      if (pushEnhClient(&enhClients[i], B)){
        last_comms = millis();
      }
    }

    const std::vector<uint8_t> sequence = bus.push(B);

    // push UART data to all connected readonly clients
    for (int i = 0; i < MAX_SRV_CLIENTS; i++){
      // if client.availableForWrite() was 0 (congested)
      // and increased since then,
      // ensure write space is sufficient:
      if (serverClientsMSG[i].availableForWrite() >= 1) {
        serverClientsMSG[i].write(sequence.data(), sequence.size());
        last_comms = millis();
      }
    }

  }

}
