#include <ArduinoOTA.h>
#include <IotWebConf.h>
#include <IotWebConfUsing.h>
#include <Preferences.h>
#include "main.hpp"
#include "enhanced.hpp"
#include "schedule.hpp"
#include "bus.hpp"
#include "mqtt.hpp"

Preferences preferences;

#ifdef ESP32
#include <esp_task_wdt.h>
#include <ESPmDNS.h>
#include "esp32c3/rom/rtc.h"
#include <IotWebConfESP32HTTPUpdateServer.h>

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

#define STRING_LEN 128

#ifdef ESP32
TaskHandle_t Task1;
#endif

#define CONFIG_VERSION "eea"
DNSServer dnsServer;
WebServer configServer(80);

char pwm_value_string[8];

char mqtt_server[STRING_LEN];
char mqtt_user[STRING_LEN];
char mqtt_pass[STRING_LEN];

IotWebConf iotWebConf(HOSTNAME, &dnsServer, &configServer, "", CONFIG_VERSION);
IotWebConfNumberParameter pwm_value_param = IotWebConfNumberParameter("PWM value", "pwm_value", pwm_value_string, 8, "130", "1..255", "min='1' max='255' step='1'");

IotWebConfParameterGroup mqttGroup = IotWebConfParameterGroup("mqtt", "MQTT configuration");
IotWebConfTextParameter mqttServerParam = IotWebConfTextParameter("MQTT server", "mqtt_server", mqtt_server, STRING_LEN, "text", nullptr, "server.lan");
IotWebConfTextParameter mqttUserParam = IotWebConfTextParameter("MQTT user", "mqtt_user", mqtt_user, STRING_LEN, "text", nullptr, "roger");
IotWebConfPasswordParameter mqttPasswordParam = IotWebConfPasswordParameter("MQTT password", "mqtt_pass", mqtt_pass, STRING_LEN, "text", nullptr, "password");

WiFiServer wifiServer(3333);
WiFiServer wifiServerRO(3334);
WiFiServer wifiServerEnh(3335);
WiFiServer statusServer(5555);
WiFiClient serverClients[MAX_SRV_CLIENTS];
WiFiClient serverClientsRO[MAX_SRV_CLIENTS];
WiFiClient enhClients[MAX_SRV_CLIENTS];

unsigned long last_comms = 0;

bool needMqttConnect = false;
unsigned long lastMqttConnectionAttempt = 0;
unsigned long lastMqttUpdate = 0;

struct MqttValues
{
  // ebus/device
  uint32_t pwm_value = 0;
  unsigned long uptime = 0;
  unsigned long loop_duration = 0;
  unsigned long loop_duration_max = 0;
  uint32_t free_heap = 0;
  int reset_code = -1;

  // ebus/device/firmware
  const char* version = AUTO_VERSION;
  const char* async = USE_ASYNCHRONOUS ? "true" : "false";
  const char* software_serial = USE_SOFTWARE_SERIAL ? "true" : "false";

  // ebus/device/wifi
  unsigned long last_connect = 0;
  int reconnect_count = 0;
  int8_t rssi = 0;

  // ebus/arbitration
  int total = 0;

  int won = 0;
  float wonPercent = 0;
  int restarts1 = 0;
  int restarts2;
  int won1 = 0;
  int won2;

  int lost = 0;
  float lostPercent = 0;
  int lost1 = 0;
  int lost2 = 0;
  int errors = 0;
  int late = 0;

};

MqttValues mqttValues;
MqttValues lastMqttValues;
bool initMqttValues = true;

bool connectMqtt() {
  if (mqttClient.connected())
    return true;

  unsigned long now = millis();

  if (1000 > now - lastMqttConnectionAttempt)
    return false;

  mqttClient.connect();

  if (!mqttClient.connected()) {
    lastMqttConnectionAttempt = now;
    return false;
  }

  return true;
}

void wifiConnected() {
  mqttValues.last_connect = millis();
  mqttValues.reconnect_count++;
  needMqttConnect = true;
}

int random_ch() {
#ifdef ESP32
  return esp_random() % 13 + 1 ;
#else
  return ESP8266TrueRandom.random(1, 13);
#endif
}

void wdt_start() {
#ifdef ESP32
  esp_task_wdt_init(6, true);
#else
  ESP.wdtDisable();
#endif
}

void wdt_feed() {
#ifdef ESP32
  esp_task_wdt_reset();
#else
  ESP.wdtFeed();
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

void set_pwm(uint8_t value) {
#ifdef PWM_PIN
  ledcWrite(PWM_CHANNEL, value);
  schedule.resetStatistics();
#endif
}

uint32_t get_pwm() {
#ifdef PWM_PIN
  return ledcRead(PWM_CHANNEL);
#endif
  return 0;
}

void reset() {
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

  mqttValues.loop_duration = ((1 - ALPHA) * mqttValues.loop_duration + (ALPHA * delta));

  if (delta > mqttValues.loop_duration_max) {
    mqttValues.loop_duration_max = delta;
  }
}

void data_process() {
  loop_duration();

  //check clients for data
  for (int i = 0; i < MAX_SRV_CLIENTS; i++){
    handleClient(&serverClients[i]);
    handleEnhClient(&enhClients[i]);
  }

  // check schedule for data
  schedule.processSend();

  // check queue for data
  BusType::data d;
  if (Bus.read(d)) {

    // push data to schedule
    if (schedule.processReceive(d._enhanced, d._client, d._d)) {
      last_comms = millis();
    }

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

void data_loop(void *pvParameters) {
  while (1) {
    data_process();
  }
}

bool formValidator(iotwebconf::WebRequestWrapper* webRequestWrapper) {
  bool valid = true;

  int l = webRequestWrapper->arg(mqttServerParam.getId()).length();

  if (l > STRING_LEN - 1) {
    String tmp = "max. ";
    tmp += String(STRING_LEN);
    tmp += " characters allowed";
    mqttServerParam.errorMessage = tmp.c_str();
    valid = false;
  }

  return valid;
}

void saveParamsCallback () {

  uint8_t new_pwm_value = atoi(pwm_value_string);
  if (new_pwm_value > 0) {
    set_pwm(new_pwm_value);
    preferences.putUInt("pwm_value", new_pwm_value);
  }
  DebugSer.printf("pwm_value set: %s %d\n", pwm_value_string, new_pwm_value);

  String mqtt_server_tmp = String(mqtt_server);
  mqtt_server_tmp.trim();
  if (mqtt_server_tmp.length() > 0)
    preferences.putString("mqtt_server", mqtt_server_tmp.c_str());
  else
    preferences.putString("mqtt_server", "\0");

  String mqtt_user_tmp = String(mqtt_user);
  mqtt_user_tmp.trim();
  if (mqtt_user_tmp.length() > 0)
    preferences.putString("mqtt_user", mqtt_user_tmp.c_str());
  else
    preferences.putString("mqtt_user", "\0");

  String mqtt_pass_tmp = String(mqtt_pass);
  mqtt_pass_tmp.trim();
  if (mqtt_pass_tmp.length() > 0)
    preferences.putString("mqtt_pass", mqtt_pass_tmp.c_str());
  else
    preferences.putString("mqtt_pass", "\0");
}

char* status_string() {
  static char status[1024];

  int pos = 0;

  pos += sprintf(status + pos, "async_mode: %s\n", USE_ASYNCHRONOUS ? "true" : "false");
  pos += sprintf(status + pos, "software_serial_mode: %s\n", USE_SOFTWARE_SERIAL ? "true" : "false");
  pos += sprintf(status + pos, "uptime: %ld ms\n", millis());
  pos += sprintf(status + pos, "last_connect_time: %ld ms\n", mqttValues.last_connect);
  pos += sprintf(status + pos, "reconnect_count: %d \n", mqttValues.reconnect_count);
  pos += sprintf(status + pos, "rssi: %d dBm\n", WiFi.RSSI());
  pos += sprintf(status + pos, "free_heap: %d B\n", ESP.getFreeHeap());
  pos += sprintf(status + pos, "reset_code: %d\n", mqttValues.reset_code);
  pos += sprintf(status + pos, "loop_duration: %ld us\r\n", mqttValues.loop_duration);
  pos += sprintf(status + pos, "max_loop_duration: %ld us\r\n", mqttValues.loop_duration_max);
  pos += sprintf(status + pos, "version: %s\r\n", AUTO_VERSION);
  pos += sprintf(status + pos, "nbr_arbitrations: %i\r\n", (int)Bus._nbrArbitrations);
  pos += sprintf(status + pos, "nbr_restarts1: %i\r\n", (int)Bus._nbrRestarts1);
  pos += sprintf(status + pos, "nbr_restarts2: %i\r\n", (int)Bus._nbrRestarts2);
  pos += sprintf(status + pos, "nbr_lost1: %i\r\n", (int)Bus._nbrLost1);
  pos += sprintf(status + pos, "nbr_lost2: %i\r\n", (int)Bus._nbrLost2);
  pos += sprintf(status + pos, "nbr_won1: %i\r\n", (int)Bus._nbrWon1);
  pos += sprintf(status + pos, "nbr_won2: %i\r\n", (int)Bus._nbrWon2);
  pos += sprintf(status + pos, "nbr_late: %i\r\n", (int)Bus._nbrLate);
  pos += sprintf(status + pos, "nbr_errors: %i\r\n", (int)Bus._nbrErrors);
  pos += sprintf(status + pos, "pwm_value: %i\r\n", get_pwm());
  pos += sprintf(status + pos, "mqtt_server: %s\r\n", mqtt_server);
  pos += sprintf(status + pos, "mqtt_user: %s\r\n", mqtt_user);
  pos += sprintf(status + pos, "mqtt_pass: %s\r\n", mqtt_pass[0] != '\0' ? "YES" : "NO");

  return status;
}

void handleStatus() {
  configServer.send(200, "text/plain", status_string());
}

void publishValues() {

  // ebus/device
  // TODO ebus address
  mqttValues.pwm_value = get_pwm();
  publishTopic(initMqttValues, "ebus/device/pwm_value", lastMqttValues.pwm_value, mqttValues.pwm_value);

  mqttValues.uptime = millis();
  publishTopic(initMqttValues, "ebus/device/uptime", lastMqttValues.uptime, mqttValues.uptime);

  // TODO average of duration
  publishTopic(initMqttValues, "ebus/device/loop_duration", lastMqttValues.loop_duration, mqttValues.loop_duration);

  publishTopic(initMqttValues, "ebus/device/loop_duration_max", lastMqttValues.loop_duration_max, mqttValues.loop_duration_max);

  mqttValues.free_heap = ESP.getFreeHeap();
  publishTopic(initMqttValues, "ebus/device/free_heap", lastMqttValues.free_heap, mqttValues.free_heap);

  publishTopic(initMqttValues, "ebus/device/reset_code", lastMqttValues.reset_code, mqttValues.reset_code);

  // ebus/device/firmware
  publishTopic(initMqttValues, "ebus/device/firmware/version", lastMqttValues.version, mqttValues.version);

  publishTopic(initMqttValues, "ebus/device/firmware/async", lastMqttValues.async, mqttValues.async);

  publishTopic(initMqttValues, "ebus/device/firmware/software_serial", lastMqttValues.software_serial, mqttValues.software_serial);

  // ebus/device/wifi
  publishTopic(initMqttValues, "ebus/device/wifi/last_connect", lastMqttValues.last_connect, mqttValues.last_connect);

  publishTopic(initMqttValues, "ebus/device/wifi/reconnect_count", lastMqttValues.reconnect_count, mqttValues.reconnect_count);

  mqttValues.rssi = WiFi.RSSI();
  publishTopic(initMqttValues, "ebus/device/wifi/rssi", lastMqttValues.rssi, mqttValues.rssi);

  // ebus/arbitration
  mqttValues.total = Bus._nbrArbitrations;
  publishTopic(initMqttValues, "ebus/arbitration/total", lastMqttValues.total, mqttValues.total);

  mqttValues.won = Bus._nbrWon1 + Bus._nbrWon2;
  publishTopic(initMqttValues, "ebus/arbitration/won", lastMqttValues.won, mqttValues.won);

  mqttValues.wonPercent = mqttValues.won / (float)mqttValues.total * 100.0f;
  publishTopic(initMqttValues, "ebus/arbitration/won/percent", lastMqttValues.wonPercent, mqttValues.wonPercent);

  mqttValues.restarts1 = Bus._nbrRestarts1;
  publishTopic(initMqttValues, "ebus/arbitration/won/restarts1", lastMqttValues.restarts1, mqttValues.restarts1);

  mqttValues.restarts2 = Bus._nbrRestarts2;
  publishTopic(initMqttValues, "ebus/arbitration/won/restarts2", lastMqttValues.restarts2, mqttValues.restarts2);

  mqttValues.won1 = Bus._nbrWon1;
  publishTopic(initMqttValues, "ebus/arbitration/won/won1", lastMqttValues.won1, mqttValues.won1);

  mqttValues.won2 = Bus._nbrWon2;
  publishTopic(initMqttValues, "ebus/arbitration/won/won2", lastMqttValues.won2, mqttValues.won2);

  mqttValues.lost = mqttValues.total - mqttValues.won;
  publishTopic(initMqttValues, "ebus/arbitration/lost", lastMqttValues.lost, mqttValues.lost);

  mqttValues.lostPercent = 100.0f - mqttValues.wonPercent;
  publishTopic(initMqttValues, "ebus/arbitration/lost/percent", lastMqttValues.lostPercent, mqttValues.lostPercent);

  mqttValues.lost1 = Bus._nbrLost1;
  publishTopic(initMqttValues, "ebus/arbitration/lost/lost1", lastMqttValues.lost1, mqttValues.lost1);

  mqttValues.lost2 = Bus._nbrLost2;
  publishTopic(initMqttValues, "ebus/arbitration/lost/lost2", lastMqttValues.lost2, mqttValues.lost2);

  mqttValues.late = Bus._nbrLate;
  publishTopic(initMqttValues, "ebus/arbitration/lost/late", lastMqttValues.late, mqttValues.late);

  mqttValues.errors = Bus._nbrErrors;
  publishTopic(initMqttValues, "ebus/arbitration/lost/errors", lastMqttValues.errors, mqttValues.errors);

  lastMqttValues = mqttValues;
  initMqttValues = false;
}

void handleRoot() {
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

void setup() {
  preferences.begin("esp-ebus", false);

  check_reset();

#ifdef ESP32
  mqttValues.reset_code = rtc_get_reset_reason(0);
#else
  mqttValues.reset_code = (int) ESP.getResetInfoPtr();
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
    preferences.putString("mqtt_server", "\0");
    preferences.putString("mqtt_user", "\0");
    preferences.putString("mqtt_pass", "\0");

    iotWebConf.saveConfig();
  } else {
    iotWebConf.skipApStartup();
  }

  int wifi_ch = random_ch();
  DebugSer.printf("Channel for AP mode: %d\n", wifi_ch);
  WiFi.channel(wifi_ch); // doesn't work, https://github.com/prampec/IotWebConf/issues/286

  mqttGroup.addItem(&mqttServerParam);
  mqttGroup.addItem(&mqttUserParam);
  mqttGroup.addItem(&mqttPasswordParam);

  iotWebConf.addSystemParameter(&pwm_value_param);
  iotWebConf.addParameterGroup(&mqttGroup);
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.setConfigSavedCallback(&saveParamsCallback);
  iotWebConf.getApTimeoutParameter()->visible = true;
  iotWebConf.setWifiConnectionTimeoutMs(7000);
  iotWebConf.setWifiConnectionCallback(&wifiConnected);

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

  mqttClient.onConnect(onMqttConnect);
  // mqttClient.onDisconnect(onMqttDisconnect);
  // mqttClient.onSubscribe(onMqttSubscribe);
  // mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  // mqttClient.onPublish(onMqttPublish);

  if (mqtt_server[0] != '\0')
    mqttClient.setServer(mqtt_server, 1883);

  if (mqtt_user[0] != '\0' && mqtt_pass[0] != '\0')
    mqttClient.setCredentials(mqtt_user, mqtt_pass);
  else if (mqtt_user[0] != '\0')
    mqttClient.setCredentials(mqtt_user);

  wifiServer.begin();
  wifiServerRO.begin();
  wifiServerEnh.begin();
  statusServer.begin();

  ArduinoOTA.begin();

  MDNS.end();
  MDNS.begin(HOSTNAME);

  wdt_start();

  last_comms = millis();

  if (schedule.needTX())
    enableTX();

#ifdef ESP32
  xTaskCreate(data_loop, "data_loop", 10000, NULL, 1, &Task1);
#endif
}

void loop() {
  ArduinoOTA.handle();

#ifdef ESP8266
  MDNS.update();
  data_process();
#endif

  wdt_feed();

  // this should be called on all platforms
  iotWebConf.doLoop();

  if (needMqttConnect) {
    if (connectMqtt()) {
      needMqttConnect = false;
    }
  }
  else if ((iotWebConf.getState() == iotwebconf::OnLine) && (!mqttClient.connected()))
  {
    needMqttConnect = true;
  }

  if (mqttClient.connected() && millis() > lastMqttUpdate + 60 * 1000)
  {
    lastMqttUpdate = millis();
    publishValues();
    schedule.publishCounters();

    if (schedule.needTX())
      enableTX();
  }

  if (millis() > last_comms + 200*1000) {
    reset();
  }

  // Check if new client on the status server
  if (handleStatusServerRequests()) {
    // exclude handleStatusServerRequests from maxLoopDuration calculation
    // as it skews the typical loop duration and set maxLoopDuration to 0
    loop_duration();
    mqttValues.loop_duration_max = 0;
  }

  // Check if there are any new clients on the eBUS servers
  if (handleNewClient(wifiServer, serverClients)) {
    enableTX();
  }

  if (handleNewClient(wifiServerEnh, enhClients)) {
    enableTX();
  }

  handleNewClient(wifiServerRO, serverClientsRO);
}
