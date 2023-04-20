#include <ArduinoOTA.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include "main.hpp"
#include "enhanced.hpp"
#include "bus.hpp"

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
#ifdef TX_DISABLE_PIN
  pinMode(TX_DISABLE_PIN, OUTPUT);
  digitalWrite(TX_DISABLE_PIN, HIGH);
#endif
}

inline void enableTX() {
#ifdef TX_DISABLE_PIN
  digitalWrite(TX_DISABLE_PIN, LOW);
#endif
}

void reset(){
  disableTX();
  ESP.restart();
}

void reset_config() {
  WiFiManager wifiManager(Serial1);  // Send debug on Serial1
  wifiManager.resetSettings();
  reset();
}

void check_reset() {
  // check if RESET_PIN being hold low and reset
  pinMode(RESET_PIN, INPUT_PULLUP);
  unsigned long resetStart = millis();
  while(digitalRead(RESET_PIN) == 0){
    if (millis() > resetStart + RESET_MS){
      reset_config();
    }
  }
}

void setup() {
  check_reset();

  Serial.setRxBufferSize(RXBUFFERSIZE);

#ifdef ESP32
  Serial1.begin(115200, SERIAL_8N1, 8, 10);
  Serial.begin(2400, SERIAL_8N1, 21, 20);
  // ESP32 in Arduino uses heuristics to sometimes set RxFIFOFull to 1, better to be explicit
  Serial.setRxFIFOFull(1); 
  last_reset_code = rtc_get_reset_reason(0);
#elif defined(ESP8266)
  Serial1.begin(115200);
  Serial.begin(2400);
  last_reset_code = (int) ESP.getResetInfoPtr();
#endif

  Serial1.setDebugOutput(true);

  disableTX();

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

#define DEBUG_LOG_BUFFER_SIZE 4000
static char   message_buffer[DEBUG_LOG_BUFFER_SIZE];
static size_t message_buffer_position;
int DEBUG_LOG_IMPL(const char *format, ...)
{
   int ret;
   if (message_buffer_position>=DEBUG_LOG_BUFFER_SIZE)
       return 0;
   
   va_list aptr;
   va_start(aptr, format);
   ret = vsnprintf(&message_buffer[message_buffer_position], DEBUG_LOG_BUFFER_SIZE-message_buffer_position, format, aptr);
   va_end(aptr);
   message_buffer_position+=ret;
   if (message_buffer_position >= DEBUG_LOG_BUFFER_SIZE) {
     message_buffer_position=DEBUG_LOG_BUFFER_SIZE;
   }
   message_buffer[DEBUG_LOG_BUFFER_SIZE-1]=0;

   return ret;
}

#ifdef USE_ASYNCHRONOUS
#define ASYNC_MODE true
#else
#define ASYNC_MODE false
#endif

bool handleStatusServerRequests() {
  if (!statusServer.hasClient())
    return false;

  WiFiClient client = statusServer.available();

  if (client.availableForWrite() >= AVAILABLE_THRESHOLD) {
    client.printf("HTTP/1.1 200 OK\r\n\r\n");
    client.printf("async mode: %s\n", ASYNC_MODE? "true" : "false");
    client.printf("uptime: %ld ms\n", millis());
    client.printf("rssi: %d dBm\n", WiFi.RSSI());
    client.printf("free_heap: %d B\n", ESP.getFreeHeap());
    client.printf("reset_code: %d\n", last_reset_code);
    client.printf("loop_duration: %ld us\r\n", loopDuration);
    client.printf("max_loop_duration: %ld us\r\n", maxLoopDuration);
    client.printf("version: %s\r\n", AUTO_VERSION);
    if (message_buffer_position>0)
    {
      client.printf("lastmessages:\r\n%s\r\n", message_buffer);
      message_buffer[0]=0;
      message_buffer_position=0;
    }
    client.flush();
    client.stop();
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
  if (handleStatusServerRequests()) {
    // exclude handleStatusServerRequests from maxLoopDuration calculation
    // as it skews the typical loop duration and set maxLoopDuration to 0
    loop_duration();
    maxLoopDuration = 0;
  }

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

  //check queue for data
  BusType::data d;
  if (Bus.read(d)) {
    for (int i = 0; i < MAX_SRV_CLIENTS; i++){
      if (d._enhanced) {
        if (d._client == &enhClients[i]) {
          pushEnhClient(&enhClients[i], d._c, d._d, true);
        }
      }
      else {
        if (pushClient(&serverClients[i], d._d)){
          last_comms = millis();
        }
        if (pushClient(&serverClientsRO[i], d._d)){
          last_comms = millis();
        }
        if (d._client != &enhClients[i]) {
          if (pushEnhClient(&enhClients[i], d._c, d._d, d._logtoclient == &enhClients[i])){
            last_comms = millis();
          }
        }
      }
    }
  }

  loop_duration();
}
