#include "main.hpp"

#include <ArduinoOTA.h>
#include <IotWebConf.h>
#include <Preferences.h>

#include "bus.hpp"
#include "enhanced.hpp"
#include "mqtt.hpp"

#ifdef EBUS_INTERNAL
#include "schedule.hpp"
#endif

#ifdef ESP32
#include <ESPmDNS.h>
#include <IotWebConfESP32HTTPUpdateServer.h>
#include <esp_task_wdt.h>

#include "esp32c3/rom/rtc.h"

HTTPUpdateServer httpUpdater;
#else
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266TrueRandom.h>
#include <ESP8266mDNS.h>

ESP8266HTTPUpdateServer httpUpdater;
#endif

Preferences preferences;

#define ALPHA 0.3

#define PWM_CHANNEL 0
#define PWM_FREQ 10000
#define PWM_RESOLUTION 8

#define DEFAULT_AP "ebus-test"
#define DEFAULT_PASS "lectronz"
#define DEFAULT_APMODE_PASS "ebusebus"

#define DEFAULT_STATIC_IP "192.168.1.180"
#define DEFAULT_GATEWAY "192.168.1.1"
#define DEFAULT_NETMASK "255.255.255.0"

#define STRING_LEN 64
#define NUMBER_LEN 8

#ifdef ESP32
TaskHandle_t Task1;
#endif

#define CONFIG_VERSION "eea"
DNSServer dnsServer;
WebServer configServer(80);

char staticIPValue[STRING_LEN];
char ipAddressValue[STRING_LEN];
char gatewayValue[STRING_LEN];
char netmaskValue[STRING_LEN];

char pwm_value[NUMBER_LEN];

#ifdef EBUS_INTERNAL
char ebus_address[NUMBER_LEN];
static char ebus_address_values[][NUMBER_LEN] = {
    "00", "10", "30", "70", "F0", "01", "11", "31", "71",
    "F1", "03", "13", "33", "73", "F3", "07", "17", "37",
    "77", "F7", "0F", "1F", "3F", "7F", "FF"};

char comand_distance[NUMBER_LEN];
#endif

char mqtt_server[STRING_LEN];
char mqtt_user[STRING_LEN];
char mqtt_pass[STRING_LEN];

IotWebConf iotWebConf(HOSTNAME, &dnsServer, &configServer, "", CONFIG_VERSION);

iotwebconf::ParameterGroup connGroup =
    iotwebconf::ParameterGroup("conn", "Connection parameters");
iotwebconf::CheckboxParameter staticIPParam = iotwebconf::CheckboxParameter(
    "Enable Static IP", "staticIPParam", staticIPValue, STRING_LEN);
iotwebconf::TextParameter ipAddressParam =
    iotwebconf::TextParameter("IP address", "ipAddress", ipAddressValue,
                              STRING_LEN, "", DEFAULT_STATIC_IP);
iotwebconf::TextParameter gatewayParam = iotwebconf::TextParameter(
    "Gateway", "gateway", gatewayValue, STRING_LEN, "", DEFAULT_GATEWAY);
iotwebconf::TextParameter netmaskParam =
    iotwebconf::TextParameter("Subnet mask", "netmask", netmaskValue,
                              STRING_LEN, DEFAULT_NETMASK, DEFAULT_NETMASK);

iotwebconf::ParameterGroup ebusGroup =
    iotwebconf::ParameterGroup("ebus", "eBUS configuration");
iotwebconf::NumberParameter pwmParam =
    iotwebconf::NumberParameter("PWM value", "pwm_value", pwm_value, NUMBER_LEN,
                                "130", "1..255", "min='1' max='255' step='1'");
#ifdef EBUS_INTERNAL
iotwebconf::SelectParameter ebusAddressParam = iotwebconf::SelectParameter(
    "eBUS address", "ebus_address", ebus_address, NUMBER_LEN,
    reinterpret_cast<char*>(ebus_address_values),
    reinterpret_cast<char*>(ebus_address_values),
    sizeof(ebus_address_values) / NUMBER_LEN, NUMBER_LEN, "FF");
iotwebconf::NumberParameter commandDistanceParam = iotwebconf::NumberParameter(
    "Command distance", "comand_distance", comand_distance, NUMBER_LEN, "1",
    "0..60", "min='0' max='60' step='1'");
#endif

iotwebconf::ParameterGroup mqttGroup =
    iotwebconf::ParameterGroup("mqtt", "MQTT configuration");
iotwebconf::TextParameter mqttServerParam = iotwebconf::TextParameter(
    "MQTT server", "mqtt_server", mqtt_server, STRING_LEN, "", "server.lan");
iotwebconf::TextParameter mqttUserParam = iotwebconf::TextParameter(
    "MQTT user", "mqtt_user", mqtt_user, STRING_LEN, "", "roger");
iotwebconf::PasswordParameter mqttPasswordParam = iotwebconf::PasswordParameter(
    "MQTT password", "mqtt_pass", mqtt_pass, STRING_LEN, "", "password");

IPAddress ipAddress;
IPAddress gateway;
IPAddress netmask;

WiFiServer wifiServer(3333);
WiFiServer wifiServerRO(3334);
WiFiServer wifiServerEnh(3335);
WiFiServer statusServer(5555);
WiFiClient serverClients[MAX_SRV_CLIENTS];
WiFiClient serverClientsRO[MAX_SRV_CLIENTS];
WiFiClient enhClients[MAX_SRV_CLIENTS];

uint32_t last_comms = 0;

bool needMqttConnect = false;
uint32_t lastMqttConnectionAttempt = 0;
uint32_t lastMqttUpdate = 0;

// ebus/device
Track<uint32_t> uptime("ebus/device/uptime", 10);
Track<uint32_t> loopDuration("ebus/device/loop_duration", 10);
Track<uint32_t> maxLoopDuration("ebus/device/loop_duration_max", 10);
Track<uint32_t> free_heap("ebus/device/free_heap", 10);
uint32_t reset_code = -1;

// ebus/device/ebus
Track<uint32_t> pwm("ebus/device/ebus/pwm", 0);

#ifdef EBUS_INTERNAL
Track<String> ebusAddress("ebus/device/ebus/ebus_address", 0);
Track<String> commandDistance("ebus/device/ebus/comand_distance", 0);
#endif

// ebus/device/wifi
Track<uint32_t> last_connect("ebus/device/wifi/last_connect", 30);
Track<int> reconnect_count("ebus/device/wifi/reconnect_count", 30);
Track<int8_t> rssi("ebus/device/wifi/rssi", 30);

// ebus/arbitration
Track<int> nbrArbitrations("ebus/arbitration/total", 10);

Track<int> nbrWon("ebus/arbitration/won", 10);
Track<float> nbrWonPercent("ebus/arbitration/won/percent", 10);
Track<int> nbrRestarts1("ebus/arbitration/won/restarts1", 10);
Track<int> nbrRestarts2("ebus/arbitration/won/restarts2", 10);
Track<int> nbrWon1("ebus/arbitration/won/won1", 10);
Track<int> nbrWon2("ebus/arbitration/won/won2", 10);

Track<int> nbrLost("ebus/arbitration/lost", 10);
Track<float> nbrLostPercent("ebus/arbitration/lost/percent", 10);
Track<int> nbrLost1("ebus/arbitration/lost/lost1", 10);
Track<int> nbrLost2("ebus/arbitration/lost/lost2", 10);
Track<int> nbrLate("ebus/arbitration/lost/late", 10);
Track<int> nbrErrors("ebus/arbitration/lost/errors", 10);

bool connectMqtt() {
  if (mqttClient.connected()) return true;

  uint32_t now = millis();

  if (1000 > now - lastMqttConnectionAttempt) return false;

  mqttClient.connect();

  if (!mqttClient.connected()) {
    lastMqttConnectionAttempt = now;
    return false;
  }

  return true;
}

void wifiConnected() {
  last_connect = millis();
  ++reconnect_count;
  needMqttConnect = true;
}

int random_ch() {
#ifdef ESP32
  return esp_random() % 13 + 1;
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
#ifdef EBUS_INTERNAL
  schedule.resetCounters();
#endif
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
  uint32_t resetStart = millis();
  while (digitalRead(RESET_PIN) == 0) {
    if (millis() > resetStart + RESET_MS) {
      reset_config();
    }
  }
}

void loop_duration() {
  static uint32_t lastTime = 0;
  uint32_t now = micros();
  uint32_t delta = now - lastTime;

  lastTime = now;

  loopDuration = ((1 - ALPHA) * loopDuration.value() + (ALPHA * delta));

  if (delta > maxLoopDuration.value()) {
    maxLoopDuration = delta;
  }
}

void data_process() {
  loop_duration();

  // check clients for data
  for (int i = 0; i < MAX_SRV_CLIENTS; i++) {
    handleClient(&serverClients[i]);
    handleEnhClient(&enhClients[i]);
  }

#ifdef EBUS_INTERNAL
  // check schedule for data
  schedule.nextCommand();
#endif

  // check queue for data
  BusType::data d;
  if (Bus.read(d)) {
#ifdef EBUS_INTERNAL
    // push data to schedule
    if (!d._enhanced) {
      schedule.processData(d._d);
      last_comms = millis();
    }
#endif

    for (int i = 0; i < MAX_SRV_CLIENTS; i++) {
      if (d._enhanced) {
        if (d._client == &enhClients[i]) {
          if (pushEnhClient(&enhClients[i], d._c, d._d, true)) {
            last_comms = millis();
          }
        }
      } else {
        if (pushClient(&serverClients[i], d._d)) {
          last_comms = millis();
        }
        if (pushClient(&serverClientsRO[i], d._d)) {
          last_comms = millis();
        }
        if (d._client != &enhClients[i]) {
          if (pushEnhClient(&enhClients[i], d._c, d._d,
                            d._logtoclient == &enhClients[i])) {
            last_comms = millis();
          }
        }
      }
    }
  }
}

void data_loop(void* pvParameters) {
  while (1) {
    data_process();
  }
}

bool formValidator(iotwebconf::WebRequestWrapper* webRequestWrapper) {
  bool valid = true;

  if (webRequestWrapper->arg(staticIPParam.getId()).equals("selected")) {
    if (!ipAddress.fromString(webRequestWrapper->arg(ipAddressParam.getId()))) {
      ipAddressParam.errorMessage = "Please provide a valid IP address!";
      valid = false;
    }
    if (!netmask.fromString(webRequestWrapper->arg(netmaskParam.getId()))) {
      netmaskParam.errorMessage = "Please provide a valid netmask!";
      valid = false;
    }
    if (!gateway.fromString(webRequestWrapper->arg(gatewayParam.getId()))) {
      gatewayParam.errorMessage = "Please provide a valid gateway address!";
      valid = false;
    }
  }

  if (webRequestWrapper->arg(mqttServerParam.getId()).length() >
      STRING_LEN - 1) {
    String tmp = "max. ";
    tmp += String(STRING_LEN);
    tmp += " characters allowed";
    mqttServerParam.errorMessage = tmp.c_str();
    valid = false;
  }

  return valid;
}

void saveParamsCallback() {
  set_pwm(atoi(pwm_value));
  pwm = get_pwm();

#ifdef EBUS_INTERNAL
  schedule.setAddress(uint8_t(std::strtoul(ebus_address, nullptr, 16)));
  ebusAddress = ebus_address;

  schedule.setDistance(atoi(comand_distance));
  commandDistance = comand_distance;
#endif

  if (mqtt_server[0] != '\0') mqttClient.setServer(mqtt_server, 1883);

  if (mqtt_user[0] != '\0') mqttClient.setCredentials(mqtt_user, mqtt_pass);
}

void connectWifi(const char* ssid, const char* password) {
  if (staticIPParam.isChecked()) {
    bool valid = true;
    valid = valid && ipAddress.fromString(String(ipAddressValue));
    valid = valid && netmask.fromString(String(netmaskValue));
    valid = valid && gateway.fromString(String(gatewayValue));

    if (valid) WiFi.config(ipAddress, gateway, netmask);
  }

  WiFi.begin(ssid, password);
}

char* status_string() {
  static char status[1024];

  int pos = 0;

  pos += snprintf(status + pos, sizeof(status), "async_mode: %s\n",
                  USE_ASYNCHRONOUS ? "true" : "false");
  pos += snprintf(status + pos, sizeof(status), "software_serial_mode: %s\n",
                  USE_SOFTWARE_SERIAL ? "true" : "false");
  pos += snprintf(status + pos, sizeof(status), "uptime: %ld ms\n", millis());
  pos += snprintf(status + pos, sizeof(status), "last_connect_time: %u ms\n",
                  last_connect.value());
  pos += snprintf(status + pos, sizeof(status), "reconnect_count: %d \n",
                  reconnect_count.value());
  pos += snprintf(status + pos, sizeof(status), "rssi: %d dBm\n", WiFi.RSSI());
  pos += snprintf(status + pos, sizeof(status), "free_heap: %u B\n",
                  free_heap.value());
  pos += snprintf(status + pos, sizeof(status), "reset_code: %u\n", reset_code);
  pos += snprintf(status + pos, sizeof(status), "loop_duration: %u us\r\n",
                  loopDuration.value());
  pos += snprintf(status + pos, sizeof(status), "max_loop_duration: %u us\r\n",
                  maxLoopDuration.value());
  pos +=
      snprintf(status + pos, sizeof(status), "version: %s\r\n", AUTO_VERSION);
  pos += snprintf(status + pos, sizeof(status), "nbr_arbitrations: %i\r\n",
                  static_cast<int>(Bus._nbrArbitrations));
  pos += snprintf(status + pos, sizeof(status), "nbr_restarts1: %i\r\n",
                  static_cast<int>(Bus._nbrRestarts1));
  pos += snprintf(status + pos, sizeof(status), "nbr_restarts2: %i\r\n",
                  static_cast<int>(Bus._nbrRestarts2));
  pos += snprintf(status + pos, sizeof(status), "nbr_lost1: %i\r\n",
                  static_cast<int>(Bus._nbrLost1));
  pos += snprintf(status + pos, sizeof(status), "nbr_lost2: %i\r\n",
                  static_cast<int>(Bus._nbrLost2));
  pos += snprintf(status + pos, sizeof(status), "nbr_won1: %i\r\n",
                  static_cast<int>(Bus._nbrWon1));
  pos += snprintf(status + pos, sizeof(status), "nbr_won2: %i\r\n",
                  static_cast<int>(Bus._nbrWon2));
  pos += snprintf(status + pos, sizeof(status), "nbr_late: %i\r\n",
                  static_cast<int>(Bus._nbrLate));
  pos += snprintf(status + pos, sizeof(status), "nbr_errors: %i\r\n",
                  static_cast<int>(Bus._nbrErrors));
  pos += snprintf(status + pos, sizeof(status), "pwm_value: %u\r\n", get_pwm());

#ifdef EBUS_INTERNAL
  pos += snprintf(status + pos, sizeof(status), "ebus_address: %s\r\n",
                  ebus_address);
  pos += snprintf(status + pos, sizeof(status), "command_distance: %i\r\n",
                  atoi(comand_distance));
#endif

  pos += snprintf(status + pos, sizeof(status), "mqtt_connected: %s\r\n",
                  mqttClient.connected() ? "true" : "false");
  pos += snprintf(status + pos, sizeof(status), "mqtt_server: %s\r\n",
                  mqtt_server);
  pos += snprintf(status + pos, sizeof(status), "mqtt_user: %s\r\n", mqtt_user);

  return status;
}

void handleStatus() { configServer.send(200, "text/plain", status_string()); }

#ifdef EBUS_INTERNAL
void handleCommands() {
  configServer.send(200, "application/json;charset=utf-8",
                    store.getCommands().c_str());
}
#endif

void publishStatus() {
  // ebus/device
  uptime.publish();
  loopDuration.publish();
  maxLoopDuration.publish();
  free_heap.publish();
  mqttClient.publish("ebus/device/reset_code", 0, true,
                     String(reset_code).c_str());

  // ebus/device/firmware
  mqttClient.publish("ebus/device/firmware/version", 0, true, AUTO_VERSION);
  mqttClient.publish("ebus/device/firmware/sdk", 0, true, ESP.getSdkVersion());
  mqttClient.publish("ebus/device/firmware/async", 0, true,
                     USE_ASYNCHRONOUS ? "true" : "false");
  mqttClient.publish("ebus/device/firmware/software_serial", 0, true,
                     USE_SOFTWARE_SERIAL ? "true" : "false");

  // ebus/device/ebus
  pwm = get_pwm();

#ifdef EBUS_INTERNAL
  ebusAddress = ebus_address;
  commandDistance = comand_distance;
#endif
}

void publishValues() {
  // ebus/device
  free_heap = ESP.getFreeHeap();

  // ebus/device/wifi
  rssi = WiFi.RSSI();

  // ebus/arbitration
  nbrArbitrations = Bus._nbrArbitrations;

  nbrWon = Bus._nbrWon1 + Bus._nbrWon2;
  nbrWonPercent =
      nbrWon.value() / static_cast<float>(nbrArbitrations.value()) * 100.0f;
  nbrRestarts1 = Bus._nbrRestarts1;
  nbrRestarts2 = Bus._nbrRestarts2;
  nbrWon1 = Bus._nbrWon1;
  nbrWon2 = Bus._nbrWon2;

  nbrLost = nbrArbitrations.value() - nbrWon.value();
  nbrLostPercent = 100.0f - nbrWonPercent.value();
  nbrLost1 = Bus._nbrLost1;
  nbrLost2 = Bus._nbrLost2;
  nbrLate = Bus._nbrLate;
  nbrErrors = Bus._nbrErrors;
}

void handleRoot() {
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal()) {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<html><head><title>esp-eBus adapter</title>";
  s += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, "
       "user-scalable=no\"/>";
  s += "</head><body>";
  s += "<a href='/status'>Adapter status</a><br>";
#ifdef EBUS_INTERNAL
  s += "<a href='/commands'>Commands</a><br>";
#endif
  s += "<a href='/config'>Configuration</a> - user: admin password: your "
       "configured AP mode password or default: ";
  s += DEFAULT_APMODE_PASS;
  s += "<br>";
  s += "<a href='/firmware'>Firmware update</a><br>";
  s += "<br>";
  s += "For more info see project page: <a "
       "href='https://github.com/danielkucera/esp-arduino-ebus'>https://"
       "github.com/danielkucera/esp-arduino-ebus</a>";
  s += "</body></html>";

  configServer.send(200, "text/html", s);
}

bool handleStatusServerRequests() {
  if (!statusServer.hasClient()) return false;

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
  reset_code = rtc_get_reset_reason(0);
#else
  reset_code = ESP.getResetInfoPtr()->reason;
#endif

  Bus.begin();

  DebugSer.begin(115200);
  DebugSer.setDebugOutput(true);

  disableTX();

#ifdef PWM_PIN
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PWM_PIN, PWM_CHANNEL);
#endif

  if (preferences.getBool("firstboot", true)) {
    preferences.putBool("firstboot", false);

    iotWebConf.init();
    strncpy(iotWebConf.getApPasswordParameter()->valueBuffer,
            DEFAULT_APMODE_PASS, IOTWEBCONF_WORD_LEN);
    strncpy(iotWebConf.getWifiSsidParameter()->valueBuffer, "ebus-test",
            IOTWEBCONF_WORD_LEN);
    strncpy(iotWebConf.getWifiPasswordParameter()->valueBuffer, "lectronz",
            IOTWEBCONF_WORD_LEN);
    iotWebConf.saveConfig();

    WiFi.channel(
        random_ch());  // doesn't work,
                       // https://github.com/prampec/IotWebConf/issues/286
  } else {
    iotWebConf.skipApStartup();
  }

  connGroup.addItem(&staticIPParam);
  connGroup.addItem(&ipAddressParam);
  connGroup.addItem(&gatewayParam);
  connGroup.addItem(&netmaskParam);

  ebusGroup.addItem(&pwmParam);

#ifdef EBUS_INTERNAL
  ebusGroup.addItem(&ebusAddressParam);
  ebusGroup.addItem(&commandDistanceParam);
#endif

  mqttGroup.addItem(&mqttServerParam);
  mqttGroup.addItem(&mqttUserParam);
  mqttGroup.addItem(&mqttPasswordParam);

  iotWebConf.addParameterGroup(&connGroup);
  iotWebConf.addParameterGroup(&ebusGroup);
  iotWebConf.addParameterGroup(&mqttGroup);
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.setConfigSavedCallback(&saveParamsCallback);
  iotWebConf.getApTimeoutParameter()->visible = true;
  iotWebConf.setWifiConnectionTimeoutMs(7000);
  iotWebConf.setWifiConnectionHandler(&connectWifi);
  iotWebConf.setWifiConnectionCallback(&wifiConnected);

#ifdef STATUS_LED_PIN
  iotWebConf.setStatusPin(STATUS_LED_PIN);
#endif

  // -- Initializing the configuration.
  iotWebConf.init();

  // -- Set up required URL handlers on the web server.
  configServer.on("/", [] { handleRoot(); });
  configServer.on("/status", [] { handleStatus(); });

#ifdef EBUS_INTERNAL
  configServer.on("/commands", [] { handleCommands(); });
#endif

  configServer.on("/config", [] { iotWebConf.handleConfig(); });
  configServer.onNotFound([]() { iotWebConf.handleNotFound(); });

  iotWebConf.setupUpdateServer(
      [](const char* updatePath) {
        httpUpdater.setup(&configServer, updatePath);
      },
      [](const char* userName, char* password) {
        httpUpdater.updateCredentials(userName, password);
      });

  set_pwm(atoi(pwm_value));

#ifdef EBUS_INTERNAL
  schedule.setAddress(uint8_t(std::strtoul(ebus_address, nullptr, 16)));
  schedule.setDistance(atoi(comand_distance));
#endif

  while (iotWebConf.getState() != iotwebconf::NetworkState::OnLine) {
    iotWebConf.doLoop();
  }

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);

  if (mqtt_server[0] != '\0') mqttClient.setServer(mqtt_server, 1883);

  if (mqtt_user[0] != '\0') mqttClient.setCredentials(mqtt_user, mqtt_pass);

  wifiServer.begin();
  wifiServerRO.begin();
  wifiServerEnh.begin();
  statusServer.begin();

  ArduinoOTA.begin();

  MDNS.begin(HOSTNAME);

  wdt_start();

  last_comms = millis();

#ifdef EBUS_INTERNAL
  // install saved commands
  store.loadCommands();
  if (store.active()) enableTX();
#endif

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

#ifdef ESP32
  // this should be called on all platforms
  iotWebConf.doLoop();
#endif

  if (needMqttConnect) {
    if (connectMqtt()) {
      needMqttConnect = false;
      publishStatus();
    }
  } else if ((iotWebConf.getState() == iotwebconf::OnLine) &&
             (!mqttClient.connected())) {
    needMqttConnect = true;
  }

  if (mqttClient.connected()) {
    if (millis() > lastMqttUpdate + 5 * 1000) {
      lastMqttUpdate = millis();
      publishValues();

#ifdef EBUS_INTERNAL
      schedule.publishCounters();
#endif
    }
#ifdef EBUS_INTERNAL
    // Check whether new commands have been added
    store.doLoop();
    if (store.active()) enableTX();
#endif
  }

  uptime = millis();

  if (millis() > last_comms + 200 * 1000) {
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
  if (handleNewClient(&wifiServer, serverClients)) {
    enableTX();
  }
  if (handleNewClient(&wifiServerEnh, enhClients)) {
    enableTX();
  }

  handleNewClient(&wifiServerRO, serverClientsRO);
}
