#include <ArduinoOTA.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include "main.hpp"
#include "enhanced.hpp"
#include "bus.hpp"
#include <Preferences.h>

Preferences preferences;

#ifdef ESP32
  #include <esp_task_wdt.h>
  #include <ESPmDNS.h>
  #include "esp32c3/rom/rtc.h"
#else
  #include <ESP8266mDNS.h>
  #include <ESP8266TrueRandom.h>
#endif

#define ALPHA 0.3

#define PWM_CHANNEL 0
#define PWM_FREQ 10000
#define PWM_RESOLUTION 8

#define DEFAULT_AP "ebus-test"
#define DEFAULT_PASS "lectronz"

#ifdef ESP32
TaskHandle_t Task1;
#endif

WiFiManager wifiManager(DebugSer);
WiFiManagerParameter param_pwm_value("pwm_value", "PWM value", "", 6);

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
unsigned long lastConnectTime = 0;
int reconnectCount = 0;

int random_ch(){
#ifdef ESP32
  return 6;
#elif defined(ESP8266)
  return ESP8266TrueRandom.random(1, 13);
#endif
}

#ifdef ESP32
void on_connected(WiFiEvent_t event, WiFiEventInfo_t info){
  lastConnectTime = millis();
  reconnectCount++;
}
#endif

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

void set_pwm(uint8_t value){
#ifdef PWM_PIN
  ledcWrite(PWM_CHANNEL, value);
#endif
}

uint32_t get_pwm(){
#ifdef PWM_PIN
  return ledcRead(PWM_CHANNEL);
#endif
  return 0;
}

void reset(){
  disableTX();
  ESP.restart();
}

void reset_config() {
  preferences.clear();
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

void data_process(){
  loop_duration();

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
          if (pushEnhClient(&enhClients[i], d._c, d._d, true)) {
            last_comms = millis();
          }
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
}

void data_loop(void * pvParameters){
  while(1){
    data_process();
  }
}

void saveParamsCallback () {
  uint8_t new_pwm_value = atoi(param_pwm_value.getValue());
  if (new_pwm_value > 0){
    set_pwm(new_pwm_value);
    preferences.putUInt("pwm_value", new_pwm_value);
  }
  DebugSer.printf("pwm_value set: %s %d\n", param_pwm_value.getValue(), new_pwm_value);
}

void setup() {
  preferences.begin("esp-ebus", false);

  check_reset();

#ifdef ESP32
  last_reset_code = rtc_get_reset_reason(0);
#elif defined(ESP8266)
  last_reset_code = (int) ESP.getResetInfoPtr();
#endif
  Bus.begin();

  DebugSer.begin(115200);
  DebugSer.setDebugOutput(true);

  disableTX();

#ifdef PWM_PIN
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PWM_PIN, PWM_CHANNEL);
#endif

  set_pwm(preferences.getUInt("pwm_value", 130));

  if (preferences.getBool("firstboot", true)){
    preferences.putBool("firstboot", false);
    WiFi.begin(DEFAULT_AP, DEFAULT_PASS);
  }

  WiFi.enableAP(false);
  WiFi.begin();

#ifdef ESP32
  WiFi.onEvent(on_connected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
#endif

  wifiManager.setSaveParamsCallback(saveParamsCallback);
  wifiManager.addParameter(&param_pwm_value);

  wifiManager.setHostname(HOSTNAME);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setWiFiAPChannel(random_ch());
  wifiManager.autoConnect(HOSTNAME);
  wifiManager.startWebPortal();

  wifiServer.begin();
  wifiServerRO.begin();
  wifiServerEnh.begin();
  statusServer.begin();

  // Stop the Bus when the OTA update starts, because it interferes with the OTA process
  ArduinoOTA.onStart([]() {
    Bus.end();
  });
  ArduinoOTA.begin();

  MDNS.end();
  MDNS.begin(HOSTNAME);

  wdt_start();

  last_comms = millis();

#ifdef ESP32
  xTaskCreate(data_loop, "data_loop", 10000, NULL, 1, &Task1);
#endif
}

bool handleStatusServerRequests() {
  if (!statusServer.hasClient())
    return false;

  WiFiClient client = statusServer.accept();

  if (client.availableForWrite() >= AVAILABLE_THRESHOLD) {
    client.printf("async mode: %s\n", USE_ASYNCHRONOUS ? "true" : "false");
    client.printf("software serial mode: %s\n", USE_SOFTWARE_SERIAL ? "true" : "false");
    client.printf("uptime: %ld ms\n", millis());
    client.printf("last_connect_time: %ld ms\n", lastConnectTime);
    client.printf("reconnect_count: %d \n", reconnectCount);
    client.printf("rssi: %d dBm\n", WiFi.RSSI());
    client.printf("free_heap: %d B\n", ESP.getFreeHeap());
    client.printf("reset_code: %d\n", last_reset_code);
    client.printf("loop_duration: %ld us\r\n", loopDuration);
    client.printf("max_loop_duration: %ld us\r\n", maxLoopDuration);
    client.printf("version: %s\r\n", AUTO_VERSION);
    client.printf("nbr arbitrations: %i\r\n", (int)Bus._nbrArbitrations);
    client.printf("nbr restarts1: %i\r\n", (int)Bus._nbrRestarts1);
    client.printf("nbr restarts2: %i\r\n", (int)Bus._nbrRestarts2);
    client.printf("nbr lost1: %i\r\n", (int)Bus._nbrLost1);
    client.printf("nbr lost2: %i\r\n", (int)Bus._nbrLost2);
    client.printf("nbr won1: %i\r\n", (int)Bus._nbrWon1);
    client.printf("nbr won2: %i\r\n", (int)Bus._nbrWon2);
    client.printf("nbr late: %i\r\n", (int)Bus._nbrLate);
    client.printf("nbr errors: %i\r\n", (int)Bus._nbrErrors);
    client.printf("pwm_value: %i\r\n", get_pwm());

    client.flush();
    client.stop();
  }
  return true;
}

void loop() {
  ArduinoOTA.handle();

#ifdef ESP8266
  MDNS.update();

  data_process();
#endif

  wdt_feed();

#ifdef ESP32
  wifiManager.process();
#endif

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

}
