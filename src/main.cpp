#include "main.hpp"

#include <ArduinoOTA.h>
#include <IotWebConf.h>
#include <IotWebConfUsing.h>
#include <Preferences.h>

#include "bus.hpp"
#include "enhanced.hpp"

Preferences preferences;

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

char pwm_value[NUMBER_LEN];

char staticIPValue[STRING_LEN];
char ipAddressValue[STRING_LEN];
char gatewayValue[STRING_LEN];
char netmaskValue[STRING_LEN];

IotWebConf iotWebConf(HOSTNAME, &dnsServer, &configServer, "", CONFIG_VERSION);
IotWebConfNumberParameter pwmParam =
    IotWebConfNumberParameter("PWM value", "pwm_value", pwm_value, NUMBER_LEN,
                              "130", "1..255", "min='1' max='255' step='1'");

IotWebConfParameterGroup connGroup =
    IotWebConfParameterGroup("conn", "Connection parameters");
IotWebConfCheckboxParameter staticIPParam = IotWebConfCheckboxParameter(
    "Enable Static IP", "staticIPParam", staticIPValue, STRING_LEN);
IotWebConfTextParameter ipAddressParam =
    IotWebConfTextParameter("IP address", "ipAddress", ipAddressValue,
                            STRING_LEN, "", DEFAULT_STATIC_IP);
IotWebConfTextParameter gatewayParam = IotWebConfTextParameter(
    "Gateway", "gateway", gatewayValue, STRING_LEN, "", DEFAULT_GATEWAY);
IotWebConfTextParameter netmaskParam =
    IotWebConfTextParameter("Subnet mask", "netmask", netmaskValue, STRING_LEN,
                            DEFAULT_NETMASK, DEFAULT_NETMASK);

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
uint32_t last_reset_code = 0;

uint32_t loopDuration = 0;
uint32_t maxLoopDuration = 0;
uint32_t lastConnectTime = 0;
int reconnectCount = 0;

int random_ch() {
#ifdef ESP32
  return esp_random() % 13 + 1;
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

void set_pwm(uint8_t value) {
#ifdef PWM_PIN
  ledcWrite(PWM_CHANNEL, value);
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

  loopDuration = ((1 - ALPHA) * loopDuration + (ALPHA * delta));

  if (delta > maxLoopDuration) {
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

  // check queue for data
  BusType::data d;
  if (Bus.read(d)) {
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

  return valid;
}

void saveParamsCallback() { set_pwm(atoi(pwm_value)); }

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

void wifiConnected() {
  lastConnectTime = millis();
  reconnectCount++;
}

char* status_string() {
  static char status[1024];

  int pos = 0;

  pos += snprintf(status + pos, sizeof(status), "async mode: %s\n",
                  USE_ASYNCHRONOUS ? "true" : "false");
  pos += snprintf(status + pos, sizeof(status), "software serial mode: %s\n",
                  USE_SOFTWARE_SERIAL ? "true" : "false");
  pos += snprintf(status + pos, sizeof(status), "uptime: %ld ms\n", millis());
  pos += snprintf(status + pos, sizeof(status), "last_connect_time: %u ms\n",
                  lastConnectTime);
  pos += snprintf(status + pos, sizeof(status), "reconnect_count: %d \n",
                  reconnectCount);
  pos += snprintf(status + pos, sizeof(status), "rssi: %d dBm\n", WiFi.RSSI());
  pos += snprintf(status + pos, sizeof(status), "free_heap: %d B\n",
                  ESP.getFreeHeap());
  pos += snprintf(status + pos, sizeof(status), "reset_code: %u\n",
                  last_reset_code);
  pos += snprintf(status + pos, sizeof(status), "loop_duration: %u us\r\n",
                  loopDuration);
  pos += snprintf(status + pos, sizeof(status), "max_loop_duration: %u us\r\n",
                  maxLoopDuration);
  pos +=
      snprintf(status + pos, sizeof(status), "version: %s\r\n", AUTO_VERSION);
  pos += snprintf(status + pos, sizeof(status), "nbr arbitrations: %i\r\n",
                  static_cast<int>(Bus._nbrArbitrations));
  pos += snprintf(status + pos, sizeof(status), "nbr restarts1: %i\r\n",
                  static_cast<int>(Bus._nbrRestarts1));
  pos += snprintf(status + pos, sizeof(status), "nbr restarts2: %i\r\n",
                  static_cast<int>(Bus._nbrRestarts2));
  pos += snprintf(status + pos, sizeof(status), "nbr lost1: %i\r\n",
                  static_cast<int>(Bus._nbrLost1));
  pos += snprintf(status + pos, sizeof(status), "nbr lost2: %i\r\n",
                  static_cast<int>(Bus._nbrLost2));
  pos += snprintf(status + pos, sizeof(status), "nbr won1: %i\r\n",
                  static_cast<int>(Bus._nbrWon1));
  pos += snprintf(status + pos, sizeof(status), "nbr won2: %i\r\n",
                  static_cast<int>(Bus._nbrWon2));
  pos += snprintf(status + pos, sizeof(status), "nbr late: %i\r\n",
                  static_cast<int>(Bus._nbrLate));
  pos += snprintf(status + pos, sizeof(status), "nbr errors: %i\r\n",
                  static_cast<int>(Bus._nbrErrors));
  pos += snprintf(status + pos, sizeof(status), "pwm_value: %u\r\n", get_pwm());

  return status;
}

void handleStatus() { configServer.send(200, "text/plain", status_string()); }

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

void setup() {
  preferences.begin("esp-ebus", false);

  check_reset();

#ifdef ESP32
  last_reset_code = rtc_get_reset_reason(0);
#elif defined(ESP8266)
  last_reset_code = ESP.getResetInfoPtr()->reason;
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

  iotWebConf.addSystemParameter(&pwmParam);
  iotWebConf.addParameterGroup(&connGroup);
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
  configServer.on("/config", [] { iotWebConf.handleConfig(); });
  configServer.on("/param", [] { iotWebConf.handleConfig(); });
  configServer.on("/status", [] { handleStatus(); });
  configServer.onNotFound([]() { iotWebConf.handleNotFound(); });

  iotWebConf.setupUpdateServer(
      [](const char* updatePath) {
        httpUpdater.setup(&configServer, updatePath);
      },
      [](const char* userName, char* password) {
        httpUpdater.updateCredentials(userName, password);
      });

  set_pwm(atoi(pwm_value));

  while (iotWebConf.getState() != iotwebconf::NetworkState::OnLine) {
    iotWebConf.doLoop();
  }

  wifiServer.begin();
  wifiServerRO.begin();
  wifiServerEnh.begin();
  statusServer.begin();

  ArduinoOTA.begin();

  MDNS.begin(HOSTNAME);

  wdt_start();

  last_comms = millis();

#ifdef ESP32
  xTaskCreate(data_loop, "data_loop", 10000, NULL, 1, &Task1);
#endif
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
  if (handleNewClient(wifiServer, serverClients)) {
    enableTX();
  }
  if (handleNewClient(wifiServerEnh, enhClients)) {
    enableTX();
  }

  handleNewClient(wifiServerRO, serverClientsRO);
}
