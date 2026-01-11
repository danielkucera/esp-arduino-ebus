#include "main.hpp"

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <IotWebConf.h>
#include <Preferences.h>

#if defined(EBUS_INTERNAL)
#include <Ebus.h>

#include "log.hpp"
#include "mqtt.hpp"
#include "mqttha.hpp"
#include "schedule.hpp"
#else
#include "bus.hpp"
#endif

#include <ESPmDNS.h>
#include <IotWebConfESP32HTTPUpdateServer.h>
#include <esp_task_wdt.h>

#include "client.hpp"
#include "esp32c3/rom/rtc.h"
#include "esp_sntp.h"
#include "http.hpp"

HTTPUpdateServer httpUpdater;

#if !defined(EBUS_INTERNAL)
TaskHandle_t Task1;
#endif

Preferences preferences;

// minimum time of reset pin
#define RESET_MS 1000

// PWM
#define PWM_CHANNEL 0
#define PWM_FREQ 10000
#define PWM_RESOLUTION 8

// mDNS
#define HOSTNAME "esp-eBus"

// IotWebConf
// adjust this if the iotwebconf structure has changed
#define CONFIG_VERSION "eeb"

#define STRING_LEN 64
#define DNS_LEN 255
#define NUMBER_LEN 8

#define DEFAULT_APMODE_PASS "ebusebus"
#define DEFAULT_AP "ebus-test"
#define DEFAULT_PASS "lectronz"

#define DUMMY_STATIC_IP "192.168.1.180"
#define DUMMY_GATEWAY "192.168.1.1"
#define DUMMY_NETMASK "255.255.255.0"

#define DUMMY_SNTP_SERVER "pool.ntp.org"
#define DUMMY_SNTP_TIMEZONE "UTC0"

#define DUMMY_MQTT_SERVER DUMMY_GATEWAY
#define DUMMY_MQTT_USER "roger"
#define DUMMY_MQTT_PASS "password"

char unique_id[7]{};

DNSServer dnsServer;

char staticIPValue[STRING_LEN];
char ipAddressValue[STRING_LEN];
char gatewayValue[STRING_LEN];
char netmaskValue[STRING_LEN];

char sntpEnabled[STRING_LEN];
char sntpServer[DNS_LEN];
char sntpTimezone[STRING_LEN];

uint32_t pwm;
char pwm_value[NUMBER_LEN];

#if defined(EBUS_INTERNAL)
char ebus_address[NUMBER_LEN];
static char ebus_address_values[][NUMBER_LEN] = {
    "00", "10", "30", "70", "f0", "01", "11", "31", "71",
    "f1", "03", "13", "33", "73", "f3", "07", "17", "37",
    "77", "f7", "0f", "1f", "3f", "7f", "ff"};
char busisr_window[NUMBER_LEN];
char busisr_offset[NUMBER_LEN];

char inquiryOfExistenceValue[STRING_LEN];
char scanOnStartupValue[STRING_LEN];
char firstCommandAfterStartValue[NUMBER_LEN];

char mqtt_enabled[STRING_LEN];
char mqtt_server[STRING_LEN];
char mqtt_user[STRING_LEN];
char mqtt_pass[STRING_LEN];

char mqttPublishCounterValue[STRING_LEN];
char mqttPublishTimingValue[STRING_LEN];

char haEnabledValue[STRING_LEN];
#endif

IotWebConf iotWebConf(HOSTNAME, &dnsServer, &configServer, DEFAULT_APMODE_PASS,
                      CONFIG_VERSION);

iotwebconf::ParameterGroup connGroup =
    iotwebconf::ParameterGroup("conn", "Connection parameters");
iotwebconf::CheckboxParameter staticIPParam = iotwebconf::CheckboxParameter(
    "Static IP", "staticIPParam", staticIPValue, STRING_LEN);
iotwebconf::TextParameter ipAddressParam = iotwebconf::TextParameter(
    "IP address", "ipAddress", ipAddressValue, STRING_LEN, "", DUMMY_STATIC_IP);
iotwebconf::TextParameter gatewayParam = iotwebconf::TextParameter(
    "Gateway", "gateway", gatewayValue, STRING_LEN, "", DUMMY_GATEWAY);
iotwebconf::TextParameter netmaskParam = iotwebconf::TextParameter(
    "Subnet mask", "netmask", netmaskValue, STRING_LEN, "", DUMMY_NETMASK);

#if defined(EBUS_INTERNAL)
iotwebconf::ParameterGroup sntpGroup =
    iotwebconf::ParameterGroup("sntp", "SNTP configuration");
iotwebconf::CheckboxParameter sntpEnabledParam = iotwebconf::CheckboxParameter(
    "SNTP enabled", "sntpEnabled", sntpEnabled, STRING_LEN);
iotwebconf::TextParameter sntpServerParam = iotwebconf::TextParameter(
    "SNTP server", "sntpServer", sntpServer, DNS_LEN, "", DUMMY_SNTP_SERVER);
iotwebconf::TextParameter sntpTimezoneParam =
    iotwebconf::TextParameter("SNTP timezone", "sntpTimezone", sntpTimezone,
                              STRING_LEN, "", DUMMY_SNTP_TIMEZONE);
#endif

iotwebconf::ParameterGroup ebusGroup =
    iotwebconf::ParameterGroup("ebus", "eBUS configuration");
iotwebconf::NumberParameter pwmParam =
    iotwebconf::NumberParameter("PWM value", "pwm_value", pwm_value, NUMBER_LEN,
                                "130", "1..255", "min='1' max='255' step='1'");
#if defined(EBUS_INTERNAL)
iotwebconf::SelectParameter ebusAddressParam = iotwebconf::SelectParameter(
    "eBUS address", "ebus_address", ebus_address, NUMBER_LEN,
    reinterpret_cast<char*>(ebus_address_values),
    reinterpret_cast<char*>(ebus_address_values),
    sizeof(ebus_address_values) / NUMBER_LEN, NUMBER_LEN, "ff");
iotwebconf::NumberParameter busIsrWindowParam = iotwebconf::NumberParameter(
    "Bus ISR window (micro seconds)", "busisr_window", busisr_window,
    NUMBER_LEN, "4300", "4250..4500", "min='4250' max='4500' step='1'");
iotwebconf::NumberParameter busIsrOffsetParam = iotwebconf::NumberParameter(
    "Bus ISR offset (micro seconds)", "busisr_offset", busisr_offset,
    NUMBER_LEN, "80", "0..200", "min='0' max='200' step='1'");

iotwebconf::ParameterGroup scheduleGroup =
    iotwebconf::ParameterGroup("schedule", "Schedule configuration");
iotwebconf::CheckboxParameter inquiryOfExistenceParam =
    iotwebconf::CheckboxParameter("Send an inquiry of existence command",
                                  "inquiryOfExistenceParam",
                                  inquiryOfExistenceValue, STRING_LEN);
iotwebconf::CheckboxParameter scanOnStartupParam =
    iotwebconf::CheckboxParameter("Scan for eBUS devices on startup",
                                  "scanOnStartupParam", scanOnStartupValue,
                                  STRING_LEN);
iotwebconf::NumberParameter firstCommandAfterStartParam = iotwebconf::NumberParameter(
    "First command after start (seconds)", "firstCommandAfterStartParam",
    firstCommandAfterStartValue, NUMBER_LEN, "10", "5..60",
    "min='5' max='60' step='1'");

iotwebconf::ParameterGroup mqttGroup =
    iotwebconf::ParameterGroup("mqtt", "MQTT configuration");
iotwebconf::CheckboxParameter mqttEnabledParam = iotwebconf::CheckboxParameter(
    "MQTT enabled", "mqttEnabledParam", mqtt_enabled, STRING_LEN);
iotwebconf::TextParameter mqttServerParam =
    iotwebconf::TextParameter("MQTT server", "mqtt_server", mqtt_server,
                              STRING_LEN, "", DUMMY_MQTT_SERVER);
iotwebconf::TextParameter mqttUserParam = iotwebconf::TextParameter(
    "MQTT user", "mqtt_user", mqtt_user, STRING_LEN, "", DUMMY_MQTT_USER);
iotwebconf::PasswordParameter mqttPasswordParam = iotwebconf::PasswordParameter(
    "MQTT password", "mqtt_pass", mqtt_pass, STRING_LEN, "", DUMMY_MQTT_PASS);

iotwebconf::CheckboxParameter mqttPublishCounterParam =
    iotwebconf::CheckboxParameter("Publish counter", "mqttPublishCounterParam",
                                  mqttPublishCounterValue, STRING_LEN);
iotwebconf::CheckboxParameter mqttPublishTimingParam =
    iotwebconf::CheckboxParameter("Publish timing", "mqttPublishTimingParam",
                                  mqttPublishTimingValue, STRING_LEN);

iotwebconf::ParameterGroup haGroup =
    iotwebconf::ParameterGroup("ha", "Home Assistant configuration");
iotwebconf::CheckboxParameter haEnabledParam = iotwebconf::CheckboxParameter(
    "Home Assistant enabled", "haEnabledParam", haEnabledValue, STRING_LEN);
#endif

IPAddress ipAddress;
IPAddress gateway;
IPAddress netmask;

#if !defined(EBUS_INTERNAL)
WiFiServer wifiServer(3333);
WiFiClient wifiClients[MAX_WIFI_CLIENTS];

WiFiServer wifiServerEnhanced(3335);
WiFiClient wifiClientsEnhanced[MAX_WIFI_CLIENTS];

WiFiServer wifiServerReadOnly(3334);
WiFiClient wifiClientsReadOnly[MAX_WIFI_CLIENTS];
#endif

WiFiServer statusServer(5555);

volatile uint32_t last_comms = 0;

// status
uint32_t reset_code = 0;
uint32_t uptime = 0;
uint32_t free_heap = 0;
uint32_t loopDuration = 0;
uint32_t maxLoopDuration;

// wifi
uint32_t last_connect = 0;
int reconnect_count = 0;

#if defined(EBUS_INTERNAL)
// mqtt
bool needMqttConnect = false;
int mqtt_reconnect_count = 0;
uint32_t lastMqttConnectionAttempt = 0;
uint32_t lastMqttUpdate = 0;

bool connectMqtt() {
  if (mqtt.connected()) return true;

  if (1000 > millis() - lastMqttConnectionAttempt) return false;

  mqtt.connect();

  if (!mqtt.connected()) {
    lastMqttConnectionAttempt = millis();
    return false;
  }

  return true;
}
#endif

void wifiConnected() {
  last_connect = millis();
  ++reconnect_count;
#if defined(EBUS_INTERNAL)
  needMqttConnect = true;
#endif
}

void wdt_start() { esp_task_wdt_init(6, true); }

void wdt_feed() { esp_task_wdt_reset(); }

inline void disableTX() {
#if defined(TX_DISABLE_PIN)
  pinMode(TX_DISABLE_PIN, OUTPUT);
  digitalWrite(TX_DISABLE_PIN, HIGH);
#endif
}

inline void enableTX() {
#if defined(TX_DISABLE_PIN)
  digitalWrite(TX_DISABLE_PIN, LOW);
#endif
}

void set_pwm(uint8_t value) {
#if defined(PWM_PIN)
  ledcWrite(PWM_CHANNEL, value);
#if defined(EBUS_INTERNAL)
  schedule.resetCounter();
  schedule.resetTiming();
#endif
#endif
}

uint32_t get_pwm() {
#if defined(PWM_PIN)
  return ledcRead(PWM_CHANNEL);
#else
  return 0;
#endif
}

void calcUniqueId() {
  uint32_t id = 0;
  for (int i = 0; i < 6; ++i) {
    id |= ((ESP.getEfuseMac() >> (8 * (5 - i))) & 0xff) << (8 * i);
  }
  char tmp[9]{};
  snprintf(tmp, sizeof(tmp), "%08x", id);
  strncpy(unique_id, &tmp[2], 6);
}

void restart() {
  disableTX();
  ESP.restart();
}

void check_reset() {
  // check if RESET_PIN being hold low and reset
  pinMode(RESET_PIN, INPUT_PULLUP);
  uint32_t resetStart = millis();
  while (digitalRead(RESET_PIN) == 0) {
    if (millis() > resetStart + RESET_MS) {
      preferences.clear();
      restart();
    }
  }
}

void updateLastComms() { last_comms = millis(); }

void loop_duration() {
  static uint32_t lastTime = 0;
  uint32_t now = micros();
  uint32_t delta = now - lastTime;
  float alpha = 0.3;

  lastTime = now;

  loopDuration = ((1 - alpha) * loopDuration + (alpha * delta));

  if (delta > maxLoopDuration) maxLoopDuration = delta;
}

#if !defined(EBUS_INTERNAL)
void data_process() {
  loop_duration();

  // check clients for data
  for (int i = 0; i < MAX_WIFI_CLIENTS; i++) {
    handleClient(&wifiClients[i]);
    handleClientEnhanced(&wifiClientsEnhanced[i]);
  }

  // check queue for data
  BusType::data d;
  if (Bus.read(d)) {
    for (int i = 0; i < MAX_WIFI_CLIENTS; i++) {
      if (d._enhanced) {
        if (d._client == &wifiClientsEnhanced[i]) {
          if (pushClientEnhanced(&wifiClientsEnhanced[i], d._c, d._d, true)) {
            updateLastComms();
          }
        }
      } else {
        if (pushClient(&wifiClients[i], d._d)) {
          updateLastComms();
        }
        if (pushClient(&wifiClientsReadOnly[i], d._d)) {
          updateLastComms();
        }
        if (d._client != &wifiClientsEnhanced[i]) {
          if (pushClientEnhanced(&wifiClientsEnhanced[i], d._c, d._d,
                                 d._logtoclient == &wifiClientsEnhanced[i])) {
            updateLastComms();
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
#endif

#if defined(EBUS_INTERNAL)
void time_sync_notification_cb(struct timeval* tv) {
  addLog(LogLevel::INFO, "SNTP synchronized to " + String(sntpServer));
}

void initSNTP(const char* server) {
  sntp_set_sync_interval(1 * 60 * 60 * 1000UL);  // 1 hour

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, server);

  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  esp_sntp_init();
}

void setTimezone(const char* timezone) {
  if (strlen(timezone) > 0) {
    addLog(LogLevel::INFO, "Timezone set to " + String(timezone));
    setenv("TZ", timezone, 1);
    tzset();
  }
}
#endif

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
#if defined(EBUS_INTERNAL)
  if (webRequestWrapper->arg(sntpServerParam.getId()).length() > DNS_LEN - 1) {
    String tmp = "max. ";
    tmp += String(DNS_LEN);
    tmp += " characters allowed";
    sntpServerParam.errorMessage = tmp.c_str();
    valid = false;
  }

  if (webRequestWrapper->arg(sntpTimezoneParam.getId()).length() >
      STRING_LEN - 1) {
    String tmp = "max. ";
    tmp += String(STRING_LEN);
    tmp += " characters allowed";
    sntpTimezoneParam.errorMessage = tmp.c_str();
    valid = false;
  }

  if (webRequestWrapper->arg(mqttServerParam.getId()).length() >
      STRING_LEN - 1) {
    String tmp = "max. ";
    tmp += String(STRING_LEN);
    tmp += " characters allowed";
    mqttServerParam.errorMessage = tmp.c_str();
    valid = false;
  }
#endif

  return valid;
}

void saveParamsCallback() {
  set_pwm(atoi(pwm_value));
  pwm = get_pwm();

#if defined(EBUS_INTERNAL)
  ebus::handler->setSourceAddress(
      uint8_t(std::strtoul(ebus_address, nullptr, 16)));
  ebus::setBusIsrWindow(atoi(busisr_window));
  ebus::setBusIsrOffset(atoi(busisr_offset));

  if (sntpEnabledParam.isChecked()) {
    esp_sntp_stop();
    initSNTP(sntpServer);
    setTimezone(sntpTimezone);
  } else {
    esp_sntp_stop();
  }

  schedule.setSendInquiryOfExistence(inquiryOfExistenceParam.isChecked());
  schedule.setScanOnStartup(scanOnStartupParam.isChecked());
  schedule.setFirstCommandAfterStart(atoi(firstCommandAfterStartValue));

  mqtt.setEnabled(mqttEnabledParam.isChecked());
  if (!mqtt.isEnabled() && mqtt.connected()) mqtt.disconnect();
  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCredentials(mqtt_user, mqtt_pass);

  schedule.setPublishCounter(mqttPublishCounterParam.isChecked());
  schedule.setPublishTiming(mqttPublishTimingParam.isChecked());

  mqttha.setEnabled(haEnabledParam.isChecked());
  mqttha.publishDeviceInfo();
  mqttha.publishComponents();
#endif
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
  const size_t bufferSize = 1024;
  static char status[bufferSize];

  size_t pos = 0;

#if !defined(EBUS_INTERNAL)
  pos += snprintf(status + pos, bufferSize - pos, "async_mode: %s\n",
                  USE_ASYNCHRONOUS ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos, "software_serial_mode: %s\n",
                  USE_SOFTWARE_SERIAL ? "true" : "false");
#endif
  pos += snprintf(status + pos, bufferSize - pos, "unique_id: %s\n", unique_id);
  pos += snprintf(status + pos, bufferSize - pos, "chip_model: %s\n",
                  ESP.getChipModel());
  pos += snprintf(status + pos, bufferSize - pos, "chip_revision: %u\n",
                  ESP.getChipRevision());
  pos += snprintf(status + pos, bufferSize - pos, "flash_chip_size: %u B\n",
                  ESP.getFlashChipSize());
  pos += snprintf(status + pos, bufferSize - pos, "flash_chip_speed: %u Hz\n",
                  ESP.getFlashChipSpeed());
  pos += snprintf(status + pos, bufferSize - pos, "flash_chip_mode: %u\n",
                  ESP.getFlashChipMode());
  pos += snprintf(status + pos, bufferSize - pos, "clock_speed: %u Mhz\n",
                  getCpuFrequencyMhz());
  pos += snprintf(status + pos, bufferSize - pos, "apb_speed: %u Hz\n",
                  getApbFrequency());
  pos += snprintf(status + pos, bufferSize - pos, "uptime: %ld ms\n", millis());
  pos += snprintf(status + pos, bufferSize - pos, "last_connect_time: %u ms\n",
                  last_connect);
  pos += snprintf(status + pos, bufferSize - pos, "reconnect_count: %d \n",
                  reconnect_count);
  pos +=
      snprintf(status + pos, bufferSize - pos, "rssi: %d dBm\n", WiFi.RSSI());
  pos +=
      snprintf(status + pos, bufferSize - pos, "free_heap: %u B\n", free_heap);
  pos +=
      snprintf(status + pos, bufferSize - pos, "reset_code: %u\n", reset_code);
  pos += snprintf(status + pos, bufferSize - pos, "loop_duration: %u us\r\n",
                  loopDuration);
  pos += snprintf(status + pos, bufferSize - pos,
                  "max_loop_duration: %u us\r\n", maxLoopDuration);
  pos +=
      snprintf(status + pos, bufferSize - pos, "version: %s\r\n", AUTO_VERSION);

#if defined(EBUS_INTERNAL)
  pos += snprintf(status + pos, bufferSize - pos, "sntpEnabled: %s\r\n",
                  sntpEnabledParam.isChecked() ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos, "sntpServer: %s\r\n",
                  sntpServer);
  pos += snprintf(status + pos, bufferSize - pos, "sntpTimezone: %s\r\n",
                  sntpTimezone);
#endif

  pos +=
      snprintf(status + pos, bufferSize - pos, "pwm_value: %u\r\n", get_pwm());

#if defined(EBUS_INTERNAL)
  pos += snprintf(status + pos, bufferSize - pos, "ebus_address: %s\r\n",
                  ebus_address);
  pos += snprintf(status + pos, bufferSize - pos, "busisr_window: %i us\r\n",
                  atoi(busisr_window));
  pos += snprintf(status + pos, bufferSize - pos, "busisr_offset: %i us\r\n",
                  atoi(busisr_offset));

  pos +=
      snprintf(status + pos, bufferSize - pos, "inquiry_of_existence: %s\r\n",
               inquiryOfExistenceParam.isChecked() ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos, "scan_on_startup: %s\r\n",
                  scanOnStartupParam.isChecked() ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos,
                  "first_command_after_start: %i\r\n",
                  atoi(firstCommandAfterStartValue));
  pos += snprintf(status + pos, bufferSize - pos, "active_commands: %zu\r\n",
                  store.getActiveCommands());
  pos += snprintf(status + pos, bufferSize - pos, "passive_commands: %zu\r\n",
                  store.getPassiveCommands());

  pos += snprintf(status + pos, bufferSize - pos, "mqtt_enabled: %s\r\n",
                  mqttEnabledParam.isChecked() ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos, "mqtt_connected: %s\r\n",
                  mqtt.connected() ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos, "mqtt_reconnect_count: %d \n",
                  mqtt_reconnect_count);
  pos += snprintf(status + pos, bufferSize - pos, "mqtt_server: %s\r\n",
                  mqtt_server);
  pos +=
      snprintf(status + pos, bufferSize - pos, "mqtt_user: %s\r\n", mqtt_user);
  pos +=
      snprintf(status + pos, bufferSize - pos, "mqtt_publish_counter: %s\r\n",
               mqttPublishCounterParam.isChecked() ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos, "mqtt_publish_timing: %s\r\n",
                  mqttPublishTimingParam.isChecked() ? "true" : "false");

  pos += snprintf(status + pos, bufferSize - pos, "ha_enabled: %s\r\n",
                  haEnabledParam.isChecked() ? "true" : "false");
#endif

  if (pos >= bufferSize) status[bufferSize - 1] = '\0';

  return status;
}

const std::string getStatusJson() {
  std::string payload;
  JsonDocument doc;

  JsonObject Status = doc["Status"].to<JsonObject>();
  Status["Reset_Code"] = reset_code;
  Status["Uptime"] = uptime;
  Status["Free_Heap"] = free_heap;
  Status["Loop_Duration"] = loopDuration;
  Status["Loop_Duration_Max"] = maxLoopDuration;

#if !defined(EBUS_INTERNAL)
  // Arbitration
  JsonObject Arbitration = doc["Arbitration"].to<JsonObject>();
  Arbitration["Total"] = static_cast<int>(Bus._nbrArbitrations);
  Arbitration["Restarts1"] = static_cast<int>(Bus._nbrRestarts1);
  Arbitration["Restarts2"] = static_cast<int>(Bus._nbrRestarts2);
  Arbitration["Won1"] = static_cast<int>(Bus._nbrWon1);
  Arbitration["Won2"] = static_cast<int>(Bus._nbrWon2);
  Arbitration["Lost1"] = static_cast<int>(Bus._nbrLost1);
  Arbitration["Lost2"] = static_cast<int>(Bus._nbrLost2);
  Arbitration["Late"] = static_cast<int>(Bus._nbrLate);
  Arbitration["Errors"] = static_cast<int>(Bus._nbrErrors);
#endif

  // Firmware
  JsonObject Firmware = doc["Firmware"].to<JsonObject>();
  Firmware["Version"] = AUTO_VERSION;
  Firmware["SDK"] = ESP.getSdkVersion();
#if !defined(EBUS_INTERNAL)
  Firmware["Async"] = USE_ASYNCHRONOUS ? true : false;
  Firmware["Software_Serial"] = USE_SOFTWARE_SERIAL ? true : false;
#endif
  Firmware["Unique_ID"] = unique_id;
  Firmware["Clock_Speed"] = getCpuFrequencyMhz();
  Firmware["Apb_Speed"] = getApbFrequency();

  // Chip
  JsonObject Chip = doc["Chip"].to<JsonObject>();
  Chip["Chip_Model"] = ESP.getChipModel();
  Chip["Chip_Revision"] = ESP.getChipRevision();
  Chip["Flash_Chip_Size"] = ESP.getFlashChipSize();
  Chip["Flash_Chip_Speed"] = ESP.getFlashChipSpeed();
  Chip["Flash_Chip_Mode"] = ESP.getFlashChipMode();

  // WIFI
  JsonObject WIFI = doc["WIFI"].to<JsonObject>();
  WIFI["Last_Connect"] = last_connect;
  WIFI["Reconnect_Count"] = reconnect_count;
  WIFI["RSSI"] = WiFi.RSSI();

  if (staticIPParam.isChecked()) {
    WIFI["Static_IP"] = true;
    WIFI["IP_Address"] = ipAddress.toString();
    WIFI["Gateway"] = gateway.toString();
    WIFI["Netmask"] = netmask.toString();
  } else {
    WIFI["Static_IP"] = false;
    WIFI["IP_Address"] = WiFi.localIP().toString();
    WIFI["Gateway"] = WiFi.gatewayIP().toString();
    WIFI["Netmask"] = WiFi.subnetMask().toString();
  }
  WIFI["SSID"] = WiFi.SSID();
  WIFI["BSSID"] = WiFi.BSSIDstr();
  WIFI["Channel"] = WiFi.channel();
  WIFI["Hostname"] = WiFi.getHostname();
  WIFI["MAC_Address"] = WiFi.macAddress();

// SNTP
#if defined(EBUS_INTERNAL)
  JsonObject SNTP = doc["SNTP"].to<JsonObject>();
  SNTP["Enabled"] = sntpEnabledParam.isChecked();
  SNTP["Server"] = sntpServer;
  SNTP["Timezone"] = sntpTimezone;
#endif

  // eBUS
  JsonObject eBUS = doc["eBUS"].to<JsonObject>();
  eBUS["PWM"] = get_pwm();
#if defined(EBUS_INTERNAL)
  eBUS["Ebus_Address"] = ebus_address;
  eBUS["BusIsr_Window"] = atoi(busisr_window);
  eBUS["BusIsr_Offset"] = atoi(busisr_offset);

  // Schedule
  JsonObject Schedule = doc["Schedule"].to<JsonObject>();
  Schedule["Inquiry_Of_Existence"] = inquiryOfExistenceParam.isChecked();
  Schedule["Scan_On_Startup"] = scanOnStartupParam.isChecked();
  Schedule["First_Command_After_Start"] = atoi(firstCommandAfterStartValue);
  Schedule["Active_Commands"] = store.getActiveCommands();
  Schedule["Passive_Commands"] = store.getPassiveCommands();

  // MQTT
  JsonObject MQTT = doc["MQTT"].to<JsonObject>();
  MQTT["Enabled"] = mqttEnabledParam.isChecked();
  MQTT["Server"] = mqtt_server;
  MQTT["User"] = mqtt_user;
  MQTT["Connected"] = mqtt.connected();
  MQTT["Reconnect_Count"] = mqtt_reconnect_count;
  MQTT["Publish_Counter"] = mqttPublishCounterParam.isChecked();
  MQTT["Publish_Timing"] = mqttPublishTimingParam.isChecked();

  // HomeAssistant
  JsonObject HomeAssistant = doc["Home_Assistant"].to<JsonObject>();
  HomeAssistant["Enabled"] = haEnabledParam.isChecked();
#endif

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
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
  DebugSer.begin(115200);
  DebugSer.setDebugOutput(true);

  preferences.begin("esp-ebus", false);

  check_reset();

  reset_code = rtc_get_reset_reason(0);

  calcUniqueId();

#if defined(EBUS_INTERNAL)
  ebus::setupBusIsr(UART_NUM_1, UART_RX, UART_TX, 1, 0);
#else
  Bus.begin();
#endif

  disableTX();

#if defined(PWM_PIN)
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PWM_PIN, PWM_CHANNEL);
#endif

  // IotWebConf
  connGroup.addItem(&staticIPParam);
  connGroup.addItem(&ipAddressParam);
  connGroup.addItem(&gatewayParam);
  connGroup.addItem(&netmaskParam);

#if defined(EBUS_INTERNAL)
  sntpGroup.addItem(&sntpEnabledParam);
  sntpGroup.addItem(&sntpServerParam);
  sntpGroup.addItem(&sntpTimezoneParam);
#endif

  ebusGroup.addItem(&pwmParam);

#if defined(EBUS_INTERNAL)
  ebusGroup.addItem(&ebusAddressParam);
  ebusGroup.addItem(&busIsrWindowParam);
  ebusGroup.addItem(&busIsrOffsetParam);

  scheduleGroup.addItem(&inquiryOfExistenceParam);
  scheduleGroup.addItem(&scanOnStartupParam);
  scheduleGroup.addItem(&firstCommandAfterStartParam);

  mqttGroup.addItem(&mqttEnabledParam);
  mqttGroup.addItem(&mqttServerParam);
  mqttGroup.addItem(&mqttUserParam);
  mqttGroup.addItem(&mqttPasswordParam);

  mqttGroup.addItem(&mqttPublishCounterParam);
  mqttGroup.addItem(&mqttPublishTimingParam);

  haGroup.addItem(&haEnabledParam);
#endif

  iotWebConf.addParameterGroup(&connGroup);
#if defined(EBUS_INTERNAL)
  iotWebConf.addParameterGroup(&sntpGroup);
#endif
  iotWebConf.addParameterGroup(&ebusGroup);
#if defined(EBUS_INTERNAL)
  iotWebConf.addParameterGroup(&scheduleGroup);
  iotWebConf.addParameterGroup(&mqttGroup);
  iotWebConf.addParameterGroup(&haGroup);
#endif
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.setConfigSavedCallback(&saveParamsCallback);
  iotWebConf.getApTimeoutParameter()->visible = true;
  iotWebConf.setWifiConnectionTimeoutMs(7000);
  iotWebConf.setWifiConnectionHandler(&connectWifi);
  iotWebConf.setWifiConnectionCallback(&wifiConnected);

#if defined(STATUS_LED_PIN)
  iotWebConf.setStatusPin(STATUS_LED_PIN);
#endif

  if (preferences.getBool("firstboot", true)) {
    preferences.putBool("firstboot", false);

    iotWebConf.init();
    strncpy(iotWebConf.getApPasswordParameter()->valueBuffer,
            DEFAULT_APMODE_PASS, IOTWEBCONF_WORD_LEN);
    strncpy(iotWebConf.getWifiSsidParameter()->valueBuffer, DEFAULT_AP,
            IOTWEBCONF_WORD_LEN);
    strncpy(iotWebConf.getWifiPasswordParameter()->valueBuffer, DEFAULT_PASS,
            IOTWEBCONF_WORD_LEN);
    iotWebConf.saveConfig();
  } else {
    iotWebConf.skipApStartup();
    // -- Initializing the configuration.
    iotWebConf.init();
  }

  SetupHttpHandlers();

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

#if defined(EBUS_INTERNAL)
  if (sntpEnabledParam.isChecked()) {
    initSNTP(sntpServer);
    setTimezone(sntpTimezone);
  }

  mqtt.setEnabled(mqttEnabledParam.isChecked());
  mqtt.setUniqueId(unique_id);
  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCredentials(mqtt_user, mqtt_pass);

  mqttha.setUniqueId(mqtt.getUniqueId());
  mqttha.setRootTopic(mqtt.getRootTopic());
  mqttha.setWillTopic(mqtt.getWillTopic());
  mqttha.setEnabled(haEnabledParam.isChecked());

  mqttha.setThingName(iotWebConf.getThingName());
  mqttha.setThingModel(ESP.getChipModel());
  mqttha.setThingModelId("Revision: " + std::to_string(ESP.getChipRevision()));
  mqttha.setThingManufacturer("danman.eu");
  mqttha.setThingSwVersion(AUTO_VERSION);
  // mqttha.setThingHwVersion(""); // how to determine hardware version?
  mqttha.setThingConfigurationUrl(
      "http://" + std::string(WiFi.localIP().toString().c_str()) + "/");
#endif

#if !defined(EBUS_INTERNAL)
  wifiServer.begin();
  wifiServerEnhanced.begin();
  wifiServerReadOnly.begin();
#endif

  statusServer.begin();

  ArduinoOTA.begin();
  MDNS.begin(HOSTNAME);
  wdt_start();

  last_comms = millis();
  enableTX();

#if defined(EBUS_INTERNAL)
  ebus::handler->setSourceAddress(
      uint8_t(std::strtoul(ebus_address, nullptr, 16)));

  schedule.setSendInquiryOfExistence(inquiryOfExistenceParam.isChecked());
  schedule.setScanOnStartup(scanOnStartupParam.isChecked());
  schedule.setFirstCommandAfterStart(atoi(firstCommandAfterStartValue));
  schedule.setPublishCounter(mqttPublishCounterParam.isChecked());
  schedule.setPublishTiming(mqttPublishTimingParam.isChecked());
  schedule.start(ebus::request, ebus::handler);

  ebus::setBusIsrWindow(atoi(busisr_window));
  ebus::setBusIsrOffset(atoi(busisr_offset));

  ebus::serviceRunner->start();

  clientManager.start(ebus::bus, ebus::request, ebus::serviceRunner);

  ArduinoOTA.onStart([]() {
    ebus::serviceRunner->stop();
    schedule.stop();
    clientManager.stop();
  });

  store.loadCommands();  // install saved commands
  mqttha.publishComponents();
#else
  xTaskCreate(data_loop, "data_loop", 10000, NULL, 1, &Task1);
  ArduinoOTA.onStart([]() { vTaskDelete(Task1); });
#endif
}

void loop() {
  ArduinoOTA.handle();

  wdt_feed();

  iotWebConf.doLoop();

#if defined(EBUS_INTERNAL)
  if (mqtt.isEnabled()) {
    if (needMqttConnect) {
      if (connectMqtt()) {
        needMqttConnect = false;
        ++mqtt_reconnect_count;
      }

    } else if ((iotWebConf.getState() == iotwebconf::OnLine) &&
               (!mqtt.connected())) {
      needMqttConnect = true;
    }

    if (mqtt.connected()) {
      uint32_t currentMillis = millis();
      if (currentMillis > lastMqttUpdate + 10 * 1000) {
        lastMqttUpdate = currentMillis;

        mqtt.publish("state/uptime", 0, false, String(uptime).c_str());
        mqtt.publish("state/free_heap", 0, false, String(free_heap).c_str());
        mqtt.publish("state/loop_duration", 0, false,
                     String(loopDuration).c_str());

        schedule.publishCounter();
        schedule.publishTiming();
      }
      mqtt.doLoop();
    }
  } else {
    if (mqtt.connected()) {
      mqtt.disconnect();
    }
  }
#endif

  uptime = millis();
  free_heap = ESP.getFreeHeap();

  if (millis() > last_comms + 200 * 1000) {
    restart();
  }

  // Check if new client on the status server
  if (handleStatusServerRequests()) {
#if !defined(EBUS_INTERNAL)
    // exclude handleStatusServerRequests from maxLoopDuration calculation
    // as it skews the typical loop duration and set maxLoopDuration to 0
    loop_duration();
    maxLoopDuration = 0;
#endif
  }

  // Check if there are any new clients on the eBUS servers
#if defined(EBUS_INTERNAL)
  loop_duration();
#else
  handleNewClient(&wifiServer, wifiClients);
  handleNewClient(&wifiServerEnhanced, wifiClientsEnhanced);
  handleNewClient(&wifiServerReadOnly, wifiClientsReadOnly);
#endif
}
