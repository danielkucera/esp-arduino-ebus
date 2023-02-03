#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#ifdef ESP32
  #include <esp_task_wdt.h>
  #include <ESPmDNS.h>
#else
  #include <ESP8266mDNS.h>
  #include <ESP8266TrueRandom.h>
#endif

#define MAX_SRV_CLIENTS 4
#define RXBUFFERSIZE 1024
#define STACK_PROTECTOR  512 // bytes
#define HOSTNAME "esp-eBus"
#define RESET_PIN 5
#define RESET_MS 1000

#ifndef TX_DISABLE_PIN
#define TX_DISABLE_PIN 5
#endif

#ifdef ESP32
// https://esp32.com/viewtopic.php?t=19788
#define AVAILABLE_THRESHOLD 0
#else
#define AVAILABLE_THRESHOLD 1
#endif

WiFiServer wifiServer(3333);
WiFiServer wifiServerRO(3334);
WiFiServer statusServer(5555);
WiFiClient serverClients[MAX_SRV_CLIENTS];
WiFiClient serverClientsRO[MAX_SRV_CLIENTS];

unsigned long last_comms;

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
#ifdef ESP32
  Serial1.begin(115200, SERIAL_8N1, 8, 10);
#elif defined(ESP8266)
  Serial1.begin(115200);
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

  Serial.setRxBufferSize(RXBUFFERSIZE);
  Serial.begin(2400);

  wifiManager.setHostname(HOSTNAME);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setWiFiAPChannel(random_ch());
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


bool handleNewClient(WiFiServer &server, WiFiClient clients[]) {
  if (!server.hasClient())
    return false;

  // Find free/disconnected slot
  int i;
  for (i = 0; i < MAX_SRV_CLIENTS; i++) {
    if (!clients[i]) { // equivalent to !serverClients[i].connected()
      clients[i] = server.available();
      clients[i].setNoDelay(true);
      break;
    }
  }

  // No free/disconnected slot so reject
  if (i == MAX_SRV_CLIENTS) {
    server.available().println("busy");
    // hints: server.available() is a WiFiClient with short-term scope
    // when out of scope, a WiFiClient will
    // - flush() - all data will be sent
    // - stop() - automatically too
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
  if (handleNewClient(wifiServer, serverClients))
    enableTX();
  handleNewClient(wifiServerRO, serverClientsRO);

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
      if (serverClients[i].availableForWrite() >= AVAILABLE_THRESHOLD) {
        serverClients[i].write(B);
        last_comms = millis();
      }
    }
    // push UART data to all connected readonly clients
    for (int i = 0; i < MAX_SRV_CLIENTS; i++){
      // if client.availableForWrite() was 0 (congested)
      // and increased since then,
      // ensure write space is sufficient:
      if (serverClientsRO[i].availableForWrite() >= AVAILABLE_THRESHOLD) {
        serverClientsRO[i].write(B);
        last_comms = millis();
      }
    }
  }

}
