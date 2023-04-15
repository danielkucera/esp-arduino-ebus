#include <ArduinoOTA.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include "main.hpp"
#include "ebusstate.hpp"

#ifdef ESP32
  #include <esp_task_wdt.h>
  #include <ESPmDNS.h>
  #include "esp32c3/rom/rtc.h"
#else
  #include <ESP8266mDNS.h>
  #include <ESP8266TrueRandom.h>
#endif

#define ALPHA 0.3

WiFiServer wifiServer(3333);
WiFiServer wifiServerRO(3334);
WiFiServer wifiServerEnh(3335);
WiFiServer statusServer(5555);
WiFiClient serverClients[MAX_SRV_CLIENTS];
WiFiClient serverClientsRO[MAX_SRV_CLIENTS];
WiFiClient enhClients[MAX_SRV_CLIENTS];
EBusState  busState;

unsigned long last_comms = 0;
int last_reset_code = -1;

unsigned long loopDuration = 0;
unsigned long maxLoopDuration = 0;

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
  last_reset_code = rtc_get_reset_reason(0);
#elif defined(ESP8266)
  Serial1.begin(115200);
  Serial.begin(2400);
  last_reset_code = (int) ESP.getResetInfoPtr();
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
    client.printf("free_heap: %d B\n", ESP.getFreeHeap());
    client.printf("reset_code: %d\n", last_reset_code);
    client.printf("loop_duration: %ld us\r\n", loopDuration);
    client.printf("max_loop_duration: %ld us\r\n", maxLoopDuration);
    client.printf("version: %s\r\n", AUTO_VERSION);
    client.flush();
    client.stop();
    maxLoopDuration = 0;
  }
  return true;
}

void loop_duration() {
  static unsigned long lastTime = 0;
  unsigned long now = micros();
  unsigned long delta = now - lastTime;
  
  lastTime = now;

  loopDuration = ((1 - ALPHA) * loopDuration + (ALPHA * delta));

  if (delta > maxLoopDuration) {
    maxLoopDuration = delta;
  }
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
  if (handleNewClient(wifiServerEnh, enhClients)){
    enableTX();
  }

  handleNewClient(wifiServerRO, serverClientsRO);

  //check clients for data
  for (int i = 0; i < MAX_SRV_CLIENTS; i++){
    handleClient(&serverClients[i]);
    handleEnhClient(&enhClients[i]);
  }

  //check UART for data
  if (Serial.available()) {
    uint8_t bytes[ARBITRATION_BUFFER_SIZE+1];
    bytes[0]= Serial.read();
    busState.data(bytes[0]);

    // handle enhanced client that is in arbitration mode
    // as a side effect, additional data can be read from the bus, which needs to
    // send to the other clients. this data will be returned in the bytes argument
    size_t bytesread = 0;
    int arbitrated_client = -1;
    for (int i = 0; i < MAX_SRV_CLIENTS; i++){
      bytesread = arbitrateEnhClient(&enhClients[i], busState, &bytes[1]);
      if (bytesread>0){
        last_comms = millis();
        arbitrated_client = i;
        break;
      }
    }
    bytesread++; // for byte at position bytes[0]

    // push data to clients, including bytes[0] and all bytes
    // returned by arbitrateEnhClient, that start at bytes[1]
    for (size_t i = 0; i < bytesread; i++) {
      for (int j = 0; j < MAX_SRV_CLIENTS; j++){
        if (pushClient(&serverClients[j], bytes[i])){
          last_comms = millis();
        }
        if (pushClient(&serverClientsRO[j], bytes[i])){
          last_comms = millis();
        }
        if (j != arbitrated_client) {
          if (pushEnhClient(&enhClients[j], bytes[i])){
            last_comms = millis();
          }
        }
      }
    }
  }

  loop_duration();
}
