#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#ifdef ESP32
  #include <esp_task_wdt.h>
  #include <ESPmDNS.h>
#else
  #include <ESP8266mDNS.h>
#endif

#define MAX_SRV_CLIENTS 4
#define RXBUFFERSIZE 1024
#define STACK_PROTECTOR  512 // bytes
#define HOSTNAME "esp-eBus"
#define RESET_PIN 0
#define TX_DISABLE_PIN 2
#define RESET_MS 3000
 
WiFiServer wifiServer(3333);
WiFiServer wifiServerRO(3334);
WiFiServer statusServer(5555);
WiFiClient serverClients[MAX_SRV_CLIENTS];
WiFiClient serverClientsRO[MAX_SRV_CLIENTS];

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

void reset(){
  digitalWrite(TX_DISABLE_PIN, 1);
  pinMode(TX_DISABLE_PIN, INPUT_PULLUP);
  ESP.restart();
}

ICACHE_RAM_ATTR void reset_config() {
  static unsigned long reset_activated = 0;
  if (digitalRead(RESET_PIN) == LOW) {
    reset_activated = millis();
  } else {
    if (millis() > reset_activated + RESET_MS) {
      WiFiManager wifiManager;
      wifiManager.resetSettings();
      reset();
    }
  }
}
 
void setup() {
  Serial.setRxBufferSize(RXBUFFERSIZE);
  Serial.begin(2400);

  WiFi.enableAP(false);

  WiFiManager wifiManager;

  pinMode(RESET_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RESET_PIN), reset_config, CHANGE); 

  digitalWrite(TX_DISABLE_PIN, 1);
  pinMode(TX_DISABLE_PIN, OUTPUT);

  wifiManager.setHostname(HOSTNAME);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.autoConnect(HOSTNAME);
 
  wifiServer.begin();
  wifiServerRO.begin();
  statusServer.begin();

  ArduinoOTA.begin();

  MDNS.end();
  MDNS.begin(HOSTNAME);

  wdt_start();

  last_comms = millis();
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
    // enable TX
    digitalWrite(TX_DISABLE_PIN, 0);

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

  //check if there are any new clients for readonly server
  if (wifiServerRO.hasClient()) {

    //find free/disconnected spot
    int i;
    for (i = 0; i < MAX_SRV_CLIENTS; i++)
      if (!serverClientsRO[i]) { // equivalent to !serverClients[i].connected()
        serverClientsRO[i] = wifiServerRO.available();
        serverClientsRO[i].setNoDelay(true);
        break;
      }

    //no free/disconnected spot so reject
    if (i == MAX_SRV_CLIENTS) {
      wifiServerRO.available().println("busy");
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
    // push UART data to all connected readonly clients
    for (int i = 0; i < MAX_SRV_CLIENTS; i++){
      // if client.availableForWrite() was 0 (congested)
      // and increased since then,
      // ensure write space is sufficient:
      if (serverClientsRO[i].availableForWrite() >= 1) {
        serverClientsRO[i].write(B);
        last_comms = millis();
      }
    }
  }

}
