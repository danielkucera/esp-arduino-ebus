#include <ArduinoOTA.h>
#include <IotWebConf.h>
#include <IotWebConfUsing.h>
#include <Preferences.h>
#include "main.hpp"
#include "enhanced.hpp"
#include "bus.hpp"

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

#define STRING_LEN 64
#define NUMBER_LEN 8

#ifdef ESP32
TaskHandle_t Task1;
#endif

#define CONFIG_VERSION "eea"
DNSServer dnsServer;
WebServer configServer(80);

char pwm_value[NUMBER_LEN];

char static_value[STRING_LEN];
char ip_value[STRING_LEN];
char netmask_value[STRING_LEN];
char gateway_value[STRING_LEN];
char dns_value[STRING_LEN];


IotWebConf iotWebConf(HOSTNAME, &dnsServer, &configServer, "", CONFIG_VERSION);
IotWebConfNumberParameter pwmValueParam = IotWebConfNumberParameter("PWM value", "pwm_value", pwm_value, NUMBER_LEN, "130", "1..255", "min='1' max='255' step='1'");

IotWebConfParameterGroup netGroup = IotWebConfParameterGroup("net", "Network");
IotWebConfCheckboxParameter staticParam = IotWebConfCheckboxParameter("Static IP", "static_value", static_value, STRING_LEN,  false);
IotWebConfTextParameter ipParam = IotWebConfTextParameter("IP address", "ip_value", ip_value, STRING_LEN, nullptr, "192.168.1.9");
IotWebConfTextParameter netmaskParam = IotWebConfTextParameter("Subnet mask", "netmask_value", netmask_value, STRING_LEN, nullptr, "255.255.255.0");
IotWebConfTextParameter gatewayParam = IotWebConfTextParameter("Gateway", "gateway_value", gateway_value, STRING_LEN, nullptr, "192.168.1.1");
IotWebConfTextParameter dnsParam = IotWebConfTextParameter("DNS Server", "dns_value", dns_value, STRING_LEN, nullptr, "192.168.1.1");

IPAddress ipAddress;
IPAddress netmask;
IPAddress gateway;
IPAddress dns;

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
  while (digitalRead(RESET_PIN) == 0) {
    if (millis() > resetStart + RESET_MS) {
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
      }
      else {
        if (pushClient(&serverClients[i], d._d)) {
          last_comms = millis();
        }
        if (pushClient(&serverClientsRO[i], d._d)) {
          last_comms = millis();
        }
        if (d._client != &enhClients[i]) {
          if (pushEnhClient(&enhClients[i], d._c, d._d, d._logtoclient == &enhClients[i])) {
            last_comms = millis();
          }
        }
      }
    }
  }
}

void data_loop(void * pvParameters) {
  while (1) {
    data_process();
  }
}

bool formValidator(iotwebconf::WebRequestWrapper* webRequestWrapper) {
  bool valid = true;

  if (staticParam.isChecked()) {
    if (!ipAddress.fromString(webRequestWrapper->arg(ipParam.getId()))) {
      ipParam.errorMessage = "Please provide a valid IP address!";
      valid = false;
    }
    if (!netmask.fromString(webRequestWrapper->arg(netmaskParam.getId()))) {
      netmaskParam.errorMessage = "Please provide a valid Subnet mask!";
      valid = false;
    }
    if (!gateway.fromString(webRequestWrapper->arg(gatewayParam.getId()))) {
      gatewayParam.errorMessage = "Please provide a valid Gateway address!";
      valid = false;
    }
    if (!dns.fromString(webRequestWrapper->arg(dnsParam.getId()))) {
      dnsParam.errorMessage = "Please provide a valid DNS address!";
      valid = false;
    }
  }

  return valid;
}

void saveParamsCallback () {
  set_pwm(atoi(pwm_value));
}

void connectWifi(const char* ssid, const char* password) {
  bool needMDNS = false;

  if (staticParam.isChecked()) {
    ipAddress.fromString(String(ip_value));
    netmask.fromString(String(netmask_value));
    gateway.fromString(String(gateway_value));
    dns.fromString(String(dns_value));

    needMDNS = !WiFi.config(ipAddress, gateway, netmask, dns);
  }

  if (!staticParam.isChecked() || needMDNS) {
    MDNS.end();
    MDNS.begin(HOSTNAME);
  }
  
  WiFi.begin(ssid, password);
}

void wifiConnected() {
  lastConnectTime = millis();
  reconnectCount++;
}

char* status_string(){
  static char status[1024];

  int pos = 0;

  pos += sprintf(status + pos, "async mode: %s\n", USE_ASYNCHRONOUS ? "true" : "false");
  pos += sprintf(status + pos, "software serial mode: %s\n", USE_SOFTWARE_SERIAL ? "true" : "false");
  pos += sprintf(status + pos, "uptime: %ld ms\n", millis());
  pos += sprintf(status + pos, "last_connect_time: %ld ms\n", lastConnectTime);
  pos += sprintf(status + pos, "reconnect_count: %d \n", reconnectCount);
  pos += sprintf(status + pos, "rssi: %d dBm\n", WiFi.RSSI());
  pos += sprintf(status + pos, "free_heap: %d B\n", ESP.getFreeHeap());
  pos += sprintf(status + pos, "reset_code: %d\n", last_reset_code);
  pos += sprintf(status + pos, "loop_duration: %ld us\r\n", loopDuration);
  pos += sprintf(status + pos, "max_loop_duration: %ld us\r\n", maxLoopDuration);
  pos += sprintf(status + pos, "version: %s\r\n", AUTO_VERSION);
  pos += sprintf(status + pos, "nbr arbitrations: %i\r\n", (int)Bus._nbrArbitrations);
  pos += sprintf(status + pos, "nbr restarts1: %i\r\n", (int)Bus._nbrRestarts1);
  pos += sprintf(status + pos, "nbr restarts2: %i\r\n", (int)Bus._nbrRestarts2);
  pos += sprintf(status + pos, "nbr lost1: %i\r\n", (int)Bus._nbrLost1);
  pos += sprintf(status + pos, "nbr lost2: %i\r\n", (int)Bus._nbrLost2);
  pos += sprintf(status + pos, "nbr won1: %i\r\n", (int)Bus._nbrWon1);
  pos += sprintf(status + pos, "nbr won2: %i\r\n", (int)Bus._nbrWon2);
  pos += sprintf(status + pos, "nbr late: %i\r\n", (int)Bus._nbrLate);
  pos += sprintf(status + pos, "nbr errors: %i\r\n", (int)Bus._nbrErrors);
  pos += sprintf(status + pos, "pwm_value: %i\r\n", get_pwm());

  return status;
}

void handleStatus() {
  configServer.send(200, "text/plain", status_string());
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
  last_reset_code = rtc_get_reset_reason(0);
#else
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

  if (preferences.getBool("firstboot", true)) {
    preferences.putBool("firstboot", false);
    
    iotWebConf.init();
    strncpy(iotWebConf.getApPasswordParameter()->valueBuffer, DEFAULT_APMODE_PASS, IOTWEBCONF_WORD_LEN);
    strncpy(iotWebConf.getWifiSsidParameter()->valueBuffer, "ebus-test", IOTWEBCONF_WORD_LEN);
    strncpy(iotWebConf.getWifiPasswordParameter()->valueBuffer, "lectronz", IOTWEBCONF_WORD_LEN);
    iotWebConf.saveConfig();

    WiFi.channel(random_ch()); // doesn't work, https://github.com/prampec/IotWebConf/issues/286
  } 
  else {
    iotWebConf.skipApStartup();
  }

  netGroup.addItem(&staticParam);
  netGroup.addItem(&ipParam);
  netGroup.addItem(&netmaskParam);
  netGroup.addItem(&gatewayParam);
  netGroup.addItem(&dnsParam);

  iotWebConf.addSystemParameter(&pwmValueParam);
  iotWebConf.addParameterGroup(&netGroup);
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
  configServer.on("/", []{ handleRoot(); });
  configServer.on("/config", []{ iotWebConf.handleConfig(); });
  configServer.on("/param", []{ iotWebConf.handleConfig(); });
  configServer.on("/status", []{ handleStatus(); });
  configServer.onNotFound([](){ iotWebConf.handleNotFound(); });

  iotWebConf.setupUpdateServer(
    [](const char* updatePath) { httpUpdater.setup(&configServer, updatePath); },
    [](const char* userName, char* password) { httpUpdater.updateCredentials(userName, password); });

  set_pwm(atoi(pwm_value));

  while (iotWebConf.getState() != iotwebconf::NetworkState::OnLine){
    iotWebConf.doLoop();
  }

  wifiServer.begin();
  wifiServerRO.begin();
  wifiServerEnh.begin();
  statusServer.begin();

  ArduinoOTA.begin();

  wdt_start();

  last_comms = millis();

#ifdef ESP32
  xTaskCreate(data_loop, "data_loop", 10000, NULL, 1, &Task1);
#endif
}

void loop() {
  ArduinoOTA.handle();

#ifdef ESP8266
  if (!staticParam.isChecked())
    MDNS.update();
  
  data_process();
#endif

  wdt_feed();

  // this should be called on all platforms
  iotWebConf.doLoop();

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
  if (handleNewClient(wifiServer, serverClients)) {
    enableTX();
  }

  if (handleNewClient(wifiServerEnh, enhClients)) {
    enableTX();
  }

  handleNewClient(wifiServerRO, serverClientsRO);
}
