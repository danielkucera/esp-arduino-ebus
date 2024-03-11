#include <ArduinoOTA.h>
#include <IotWebConf.h>
#include <IotWebConfUsing.h>
#include "main.hpp"
#include "enhanced.hpp"
#include "bus.hpp"
#include <Preferences.h>

Preferences preferences;

#ifdef ESP32
  #include <esp_task_wdt.h>
  #include <ESPmDNS.h>
  #include "esp32c3/rom/rtc.h"
  #include <IotWebConfESP32HTTPUpdateServer.h>
  #include <esp32-hal.h>

  HTTPUpdateServer httpUpdater;
#else
  #include <ESP8266mDNS.h>
  #include <ESP8266TrueRandom.h>
  #include <ESP8266HTTPUpdateServer.h>

  ESP8266HTTPUpdateServer httpUpdater;
#endif

#define ALPHA 0.3

#define PWM_CHANNEL 0
#define PWM_FREQ 10000
#define PWM_RESOLUTION 8

#define DEFAULT_AP "ebus-test"
#define DEFAULT_PASS "lectronz"
#define DEFAULT_APMODE_PASS "ebusebus"

#ifdef ESP32
TaskHandle_t Task1;
#endif

#define CONFIG_VERSION "eea"
DNSServer dnsServer;
WebServer configServer(80);
char pwm_value_string[8];
IotWebConf iotWebConf(HOSTNAME, &dnsServer, &configServer, "", CONFIG_VERSION);
IotWebConfNumberParameter pwm_value_param = IotWebConfNumberParameter("PWM value", "pwm_value", pwm_value_string, 8, "130", "1..255", "min='1' max='255' step='1'");

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
  return esp_random() % 13 + 1 ;
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

  uint8_t new_pwm_value = atoi(pwm_value_string);
  if (new_pwm_value > 0){
    set_pwm(new_pwm_value);
    preferences.putUInt("pwm_value", new_pwm_value);
  }
  DebugSer.printf("pwm_value set: %s %d\n", pwm_value_string, new_pwm_value);

}

String status_string() {
  String result;

  result += "firmware_version: " + String(AUTO_VERSION) + "\r\n";
#ifdef ESP32
  result += "esp32_chip_model: " + String(ESP.getChipModel()) + " Rev " + String(ESP.getChipRevision()) + "\r\n";
  result += "esp32_chip_cores: " + String(ESP.getChipCores()) + "\r\n";
  result += "esp32_chip_temperature: " + String(temperatureRead()) + " °C\r\n";
#endif
  result += "cpu_chip_speed: " + String(ESP.getCpuFreqMHz()) + " MHz\r\n";
  result += "flash_chip_speed: " + String(ESP.getFlashChipSpeed()/1000000) + " MHz\r\n";
  result += "flash_size: " + String(ESP.getFlashChipSize()/1024/1024) + " MB\r\n";
  result += "free_heap: " + String(ESP.getFreeHeap()) + " B\r\n";
  result += "sdk_version: " + String(ESP.getSdkVersion()) + "\r\n";
  result += "wifi_sid: " + String(WiFi.SSID()) + "\r\n";
  result += "wifi_ip_address: " + String(WiFi.localIP().toString()) + "\r\n";
  result += "wifi_rssi: " + String(WiFi.RSSI()) + " dBm\r\n";
  result += "async_mode: " + String(USE_ASYNCHRONOUS ? "true" : "false") + "\r\n";
  result += "software_serial_mode: " + String(USE_SOFTWARE_SERIAL ? "true" : "false") + "\r\n";
  result += "uptime: " + String(millis()/1000) + " s\r\n";
  result += "last_connect_time: " + String(lastConnectTime) + " ms\r\n";
  result += "reconnect_count: " + String(reconnectCount) + " \r\n";
  result += "reset_code: " + String(last_reset_code) + "\r\n";
  result += "loop_duration: " + String(loopDuration) + " µs\r\n";
  result += "max_loop_duration: " + String(maxLoopDuration) + " µs\r\n";
  result += "nbr_arbitrations: " + String((int)Bus._nbrArbitrations) + "\r\n";
  result += "nbr_restarts1: " + String((int)Bus._nbrRestarts1) + "\r\n";
  result += "nbr_restarts2: " + String((int)Bus._nbrRestarts2) + "\r\n";
  result += "nbr_lost1: " + String((int)Bus._nbrLost1) + "\r\n";
  result += "nbr_lost2: " + String((int)Bus._nbrLost2) + "\r\n";
  result += "nbr_won1: " + String((int)Bus._nbrWon1) + "\r\n";
  result += "nbr_won2: " + String((int)Bus._nbrWon2) + "\r\n";
  result += "nbr_late: " + String((int)Bus._nbrLate) + "\r\n";
  result += "nbr_errors: " + String((int)Bus._nbrErrors) + "\r\n";
  result += "pwm_value: " + String(get_pwm()) + "\r\n";

  return result;
}

void handleStatus()
{
  configServer.send(200, "text/plain; charset=utf-8", status_string());
}


void handleRoot()
{
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<html><head><title>esp-eBus adapter</title>";
  s += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "</head><body>";
  s += "<a href='/status'>Adapter status</a><br>";
  s += "<a href='/config'>Configuration</a> - user: admin password: your configured AP mode password or default: ";
  s += DEFAULT_APMODE_PASS;
  s += "<br>";
  s += "<a href='/firmware'>Firmware update</a><br>";
  s += "<br>";
  s += "For more info see project page: <a href='https://github.com/danielkucera/esp-arduino-ebus'>https://github.com/danielkucera/esp-arduino-ebus</a>";
  s += "</body></html>";

  configServer.send(200, "text/html", s);
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
    iotWebConf.init();
    strncpy(iotWebConf.getApPasswordParameter()->valueBuffer, DEFAULT_APMODE_PASS, IOTWEBCONF_WORD_LEN);
    strncpy(iotWebConf.getWifiSsidParameter()->valueBuffer, "ebus-test", IOTWEBCONF_WORD_LEN);
    strncpy(iotWebConf.getWifiPasswordParameter()->valueBuffer, "lectronz", IOTWEBCONF_WORD_LEN);
    iotWebConf.saveConfig();
  } else {
    iotWebConf.skipApStartup();
  }

#ifdef ESP32
  WiFi.onEvent(on_connected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
#endif

  int wifi_ch = random_ch();
  DebugSer.printf("Channel for AP mode: %d\n", wifi_ch);
  WiFi.channel(wifi_ch); // doesn't work, https://github.com/prampec/IotWebConf/issues/286

  iotWebConf.addSystemParameter(&pwm_value_param);
  iotWebConf.setConfigSavedCallback(&saveParamsCallback);
  iotWebConf.getApTimeoutParameter()->visible = true;
  iotWebConf.setWifiConnectionTimeoutMs(7000);

#ifdef STATUS_LED_PIN
  iotWebConf.setStatusPin(STATUS_LED_PIN);
#endif

  // -- Initializing the configuration.
  iotWebConf.init();

  // -- Set up required URL handlers on the web server.
  configServer.on("/", []{ handleRoot(); });
  configServer.on("/config", []{ iotWebConf.handleConfig(); });
  configServer.on("/param", []{ iotWebConf.handleConfig(); });
  configServer.on("/status", []{ handleStatus(); });
  configServer.onNotFound([](){ iotWebConf.handleNotFound(); });

  iotWebConf.setupUpdateServer(
    [](const char* updatePath) { httpUpdater.setup(&configServer, updatePath); },
    [](const char* userName, char* password) { httpUpdater.updateCredentials(userName, password); });

  while (iotWebConf.getState() != iotwebconf::NetworkState::OnLine){
      iotWebConf.doLoop();
  }

  wifiServer.begin();
  wifiServerRO.begin();
  wifiServerEnh.begin();
  statusServer.begin();

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
    client.print(status_string());
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
  iotWebConf.doLoop();
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
