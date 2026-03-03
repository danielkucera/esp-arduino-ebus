#include "main.hpp"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_efuse.h>

#if defined(EBUS_INTERNAL)
#include <Ebus.h>

#include "ClientManager.hpp"
#include "DeviceManager.hpp"
#include "Logger.hpp"
#include "Mqtt.hpp"
#include "MqttHA.hpp"
#include "Schedule.hpp"
#include "Store.hpp"
#else
#include "BusType.hpp"
#include "client.hpp"
#endif

#include <ESPmDNS.h>
#include <esp_task_wdt.h>

#include "ConfigManager.hpp"
#include "UpgradeManager.hpp"
#include "WifiNetworkManager.hpp"
#include "esp32c3/rom/rtc.h"
#include "esp_sntp.h"
#include "http.hpp"

#if !defined(EBUS_INTERNAL)
TaskHandle_t Task1;
#endif

ConfigManager configManager;
UpgradeManager upgradeManager;
WifiNetworkManager wifiNetworkManager;

// minimum time of reset pin
#define RESET_MS 1000

// PWM
#define PWM_CHANNEL 0
#define PWM_FREQ 10000
#define PWM_RESOLUTION 8

// mDNS
#define HOSTNAME "esp-eBus"

#define STRING_LEN 64
#define DNS_LEN 255
#define NUMBER_LEN 8

#define DUMMY_STATIC_IP "192.168.1.180"
#define DUMMY_GATEWAY "192.168.1.1"
#define DUMMY_NETMASK "255.255.255.0"

#define DUMMY_SNTP_SERVER "pool.ntp.org"
#define DUMMY_SNTP_TIMEZONE "UTC0"

#define DUMMY_MQTT_SERVER DUMMY_GATEWAY
#define DUMMY_MQTT_USER "roger"
#define DUMMY_MQTT_PASS "password"

char unique_id[7]{};

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

#if defined(EBUS_INTERNAL)
// mqtt
uint32_t lastMqttUpdate = 0;
#endif

enum class AdapterHwVersionEfuse : uint8_t {
  PRE_7_0 = 0x00,
  V7_0 = 0x70,
};

static constexpr size_t ADAPTER_HW_VERSION_EFUSE_BITS = 8;
static constexpr size_t ADAPTER_HW_VERSION_EFUSE_OFFSET = 248;  // BLOCK3 bit 248..255

static const esp_efuse_desc_t ADAPTER_HW_VERSION_EFUSE_DESC = {
    EFUSE_BLK3, ADAPTER_HW_VERSION_EFUSE_OFFSET, ADAPTER_HW_VERSION_EFUSE_BITS};
static const esp_efuse_desc_t* ADAPTER_HW_VERSION_EFUSE_FIELD[] = {
    &ADAPTER_HW_VERSION_EFUSE_DESC, nullptr};
uint8_t adapterHwVersionRaw = 0xEE;
std::string adapterHwVersion = "unread";

bool parseStoredBool(const String& value) {
  return value == "selected" || value == "true" || value == "1" ||
         value == "on";
}

bool readConfigBool(const char* key, bool fallback = false) {
  return parseStoredBool(configManager.readString(key, fallback ? "selected" : ""));
}

String readConfigValue(const char* key, const char* fallback = "") {
  return configManager.readString(key, fallback);
}

void wdt_start() {
  esp_task_wdt_init(6, true);
  esp_task_wdt_add(NULL);
}

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

void set_pwm() {
  int value = configManager.readInt("pwmValue", 130);
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

std::string formatAdapterHwVersion(const uint8_t raw) {
  if (static_cast<AdapterHwVersionEfuse>(raw) == AdapterHwVersionEfuse::PRE_7_0) {
    return "pre-7.0";
  }

  const uint8_t major = (raw >> 4) & 0x0F;
  const uint8_t minor = raw & 0x0F;
  if (major <= 9 && minor <= 9) {
    return std::to_string(major) + "." + std::to_string(minor);
  }

  char tmp[8]{};
  snprintf(tmp, sizeof(tmp), "0x%02X", raw);
  return std::string(tmp);
}

void loadAdapterHwVersionFromEfuse() {
  uint8_t raw;
  const esp_err_t err =
      esp_efuse_read_field_blob(ADAPTER_HW_VERSION_EFUSE_FIELD, &raw,
                                ADAPTER_HW_VERSION_EFUSE_BITS);
  if (err != ESP_OK) {
    adapterHwVersionRaw = 0xEE;
    adapterHwVersion = "reading error";
    return;
  }

  adapterHwVersionRaw = raw;
  adapterHwVersion = formatAdapterHwVersion(raw);
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
      configManager.resetConfig();
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
  logger.info("SNTP synchronized to " +
              readConfigValue("sntpServer", DUMMY_SNTP_SERVER));
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
    logger.info("Timezone set to " + String(timezone));
    setenv("TZ", timezone, 1);
    tzset();
  }
}

const std::string getMqttStatusJson() {
  std::string payload;
  JsonDocument doc;

  doc["reset_code"] = reset_code;
  doc["uptime"] = uptime;
  doc["free_heap"] = free_heap;
  doc["loop_duration"] = loopDuration;
  doc["rssi"] = WiFi.RSSI();

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
}
#endif

void saveParamsCallback() {
  set_pwm();

#if defined(EBUS_INTERNAL)
  String ebusAddress = readConfigValue("ebusAddress", "ff");
  ebus::handler->setSourceAddress(
      uint8_t(std::strtoul(ebusAddress.c_str(), nullptr, 16)));
  ebus::setBusIsrWindow(configManager.readInt("busisrWindow", 4300));
  ebus::setBusIsrOffset(configManager.readInt("busisrOffset", 80));

  if (readConfigBool("sntpEnabled")) {
    esp_sntp_stop();
    String sntpServerValue = readConfigValue("sntpServer", DUMMY_SNTP_SERVER);
    String sntpTimezoneValue =
        readConfigValue("sntpTimezone", DUMMY_SNTP_TIMEZONE);
    initSNTP(sntpServerValue.c_str());
    setTimezone(sntpTimezoneValue.c_str());
  } else {
    esp_sntp_stop();
  }

  deviceManager.setScanOnStartup(readConfigBool("scanOnStartPrm"));

  schedule.setSendInquiryOfExistence(readConfigBool("inquiryExistPrm"));
  schedule.setFirstCommandAfterStart(
      configManager.readInt("firstCmdAfterSt", 10));

  String mqttServerValue = readConfigValue("mqttServer", DUMMY_MQTT_SERVER);
  String mqttUserValue = readConfigValue("mqttUser", DUMMY_MQTT_USER);
  String mqttPassValue = readConfigValue("mqttPass", DUMMY_MQTT_PASS);
  mqtt.setEnabled(readConfigBool("mqttEnabled"));
  mqtt.setServer(mqttServerValue.c_str(), 1883);
  mqtt.setCredentials(mqttUserValue.c_str(), mqttPassValue.c_str());
  mqtt.change();

  schedule.setPublishCounter(readConfigBool("mqttPublishCnt"));
  schedule.setPublishTiming(readConfigBool("mqttPublishTmg"));

  mqttha.setEnabled(readConfigBool("haEnabledParam"));
  mqttha.publishDeviceInfo();
  mqttha.publishComponents();
#endif
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
                  wifiNetworkManager.getLastConnect());
  pos += snprintf(status + pos, bufferSize - pos, "reconnect_count: %d \n",
                  wifiNetworkManager.getReconnectCount());
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
  pos += snprintf(status + pos, bufferSize - pos, "adapter_hw_version: %s\r\n",
                  adapterHwVersion.c_str());
  pos += snprintf(status + pos, bufferSize - pos,
                  "adapter_hw_version_raw: 0x%02X\r\n", adapterHwVersionRaw);

#if defined(EBUS_INTERNAL)
  String sntpServerValue = readConfigValue("sntpServer", DUMMY_SNTP_SERVER);
  String sntpTimezoneValue =
      readConfigValue("sntpTimezone", DUMMY_SNTP_TIMEZONE);
  pos += snprintf(status + pos, bufferSize - pos, "sntpEnabled: %s\r\n",
                  readConfigBool("sntpEnabled") ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos, "sntpServer: %s\r\n",
                  sntpServerValue.c_str());
  pos += snprintf(status + pos, bufferSize - pos, "sntpTimezone: %s\r\n",
                  sntpTimezoneValue.c_str());
#endif

  pos +=
      snprintf(status + pos, bufferSize - pos, "pwm_value: %u\r\n", get_pwm());

#if defined(EBUS_INTERNAL)
  String ebusAddress = readConfigValue("ebusAddress", "ff");
  pos += snprintf(status + pos, bufferSize - pos, "ebus_address: %s\r\n",
                  ebusAddress.c_str());
  pos += snprintf(status + pos, bufferSize - pos, "busisr_window: %i us\r\n",
                  configManager.readInt("busisrWindow", 4300));
  pos += snprintf(status + pos, bufferSize - pos, "busisr_offset: %i us\r\n",
                  configManager.readInt("busisrOffset", 80));

  pos +=
      snprintf(status + pos, bufferSize - pos, "inquiry_of_existence: %s\r\n",
               readConfigBool("inquiryExistPrm") ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos, "scan_on_startup: %s\r\n",
                  readConfigBool("scanOnStartPrm") ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos,
                  "first_command_after_start: %i\r\n",
                  configManager.readInt("firstCmdAfterSt", 10));
  pos += snprintf(status + pos, bufferSize - pos, "active_commands: %zu\r\n",
                  store.getActiveCommands());
  pos += snprintf(status + pos, bufferSize - pos, "passive_commands: %zu\r\n",
                  store.getPassiveCommands());

  pos += snprintf(status + pos, bufferSize - pos, "mqtt_enabled: %s\r\n",
                  mqtt.isEnabled() ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos, "mqtt_connected: %s\r\n",
                  mqtt.isConnected() ? "true" : "false");

  String mqttServerValue = readConfigValue("mqttServer", DUMMY_MQTT_SERVER);
  String mqttUserValue = readConfigValue("mqttUser", DUMMY_MQTT_USER);
  pos += snprintf(status + pos, bufferSize - pos, "mqtt_server: %s\r\n",
                  mqttServerValue.c_str());
  pos +=
      snprintf(status + pos, bufferSize - pos, "mqtt_user: %s\r\n",
               mqttUserValue.c_str());
  pos +=
      snprintf(status + pos, bufferSize - pos, "mqtt_publish_counter: %s\r\n",
               schedule.getPublishCounter() ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos, "mqtt_publish_timing: %s\r\n",
                  schedule.getPublishTiming() ? "true" : "false");

  pos += snprintf(status + pos, bufferSize - pos, "ha_enabled: %s\r\n",
                  mqttha.isEnabled() ? "true" : "false");
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
  Firmware["Adapter_HW_Version"] = adapterHwVersion;
  Firmware["Adapter_HW_Version_Raw"] = adapterHwVersionRaw;
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
  WIFI["Last_Connect"] = wifiNetworkManager.getLastConnect();
  WIFI["Reconnect_Count"] = wifiNetworkManager.getReconnectCount();
  WIFI["RSSI"] = WiFi.RSSI();

  if (wifiNetworkManager.isStaticIpEnabled()) {
    WIFI["Static_IP"] = true;
    WIFI["IP_Address"] = wifiNetworkManager.getConfiguredIpAddress();
    WIFI["Gateway"] = wifiNetworkManager.getConfiguredGateway();
    WIFI["Netmask"] = wifiNetworkManager.getConfiguredNetmask();
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
  SNTP["Enabled"] = readConfigBool("sntpEnabled");
  SNTP["Server"] = readConfigValue("sntpServer", DUMMY_SNTP_SERVER);
  SNTP["Timezone"] = readConfigValue("sntpTimezone", DUMMY_SNTP_TIMEZONE);
#endif

  // eBUS
  JsonObject eBUS = doc["eBUS"].to<JsonObject>();
  eBUS["PWM"] = get_pwm();
#if defined(EBUS_INTERNAL)
  eBUS["Ebus_Address"] = readConfigValue("ebusAddress", "ff");
  eBUS["BusIsr_Window"] = configManager.readInt("busisrWindow", 4300);
  eBUS["BusIsr_Offset"] = configManager.readInt("busisrOffset", 80);

  // Schedule
  JsonObject Schedule = doc["Schedule"].to<JsonObject>();
  Schedule["Inquiry_Of_Existence"] = readConfigBool("inquiryExistPrm");
  Schedule["Scan_On_Startup"] = readConfigBool("scanOnStartPrm");
  Schedule["First_Command_After_Start"] =
      configManager.readInt("firstCmdAfterSt", 10);
  Schedule["Active_Commands"] = store.getActiveCommands();
  Schedule["Passive_Commands"] = store.getPassiveCommands();

  // MQTT
  JsonObject MQTT = doc["MQTT"].to<JsonObject>();
  MQTT["Enabled"] = mqtt.isEnabled();
  MQTT["Server"] = readConfigValue("mqttServer", DUMMY_MQTT_SERVER);
  MQTT["User"] = readConfigValue("mqttUser", DUMMY_MQTT_USER);
  MQTT["Connected"] = mqtt.isConnected();
  MQTT["Publish_Counter"] = schedule.getPublishCounter();
  MQTT["Publish_Timing"] = schedule.getPublishTiming();

  // HomeAssistant
  JsonObject HomeAssistant = doc["Home_Assistant"].to<JsonObject>();
  HomeAssistant["Enabled"] = mqttha.isEnabled();
#endif

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
}

bool isCaptivePortalActive() { return wifiNetworkManager.isCaptivePortalActive(); }

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

  check_reset();

  reset_code = rtc_get_reset_reason(0);

  calcUniqueId();
  loadAdapterHwVersionFromEfuse();

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

  SetupHttpHandlers();
  configManager.begin(&configServer);
  upgradeManager.begin(&configServer);
  wifiNetworkManager.begin(&configManager);
  configServer.begin();
  upgradeManager.setPreUpgradeHook([]() {
#if defined(EBUS_INTERNAL)
    ebus::serviceRunner->stop();
    schedule.stop();
    clientManager.stop();
#else
    if (Task1 != nullptr) {
      vTaskDelete(Task1);
      Task1 = nullptr;
    }
#endif
  });

  set_pwm();

#if defined(EBUS_INTERNAL)
  if (readConfigBool("sntpEnabled")) {
    String sntpServerValue = readConfigValue("sntpServer", DUMMY_SNTP_SERVER);
    String sntpTimezoneValue =
        readConfigValue("sntpTimezone", DUMMY_SNTP_TIMEZONE);
    initSNTP(sntpServerValue.c_str());
    setTimezone(sntpTimezoneValue.c_str());
  }

  String mqttServerValue = readConfigValue("mqttServer", DUMMY_MQTT_SERVER);
  String mqttUserValue = readConfigValue("mqttUser", DUMMY_MQTT_USER);
  String mqttPassValue = readConfigValue("mqttPass", DUMMY_MQTT_PASS);
  mqtt.setEnabled(readConfigBool("mqttEnabled"));
  mqtt.setup(unique_id);
  mqtt.setServer(mqttServerValue.c_str(), 1883);
  mqtt.setCredentials(mqttUserValue.c_str(), mqttPassValue.c_str());
  mqtt.start();

  mqttha.setUniqueId(mqtt.getUniqueId());
  mqttha.setRootTopic(mqtt.getRootTopic());
  mqttha.setWillTopic(mqtt.getWillTopic());
  mqttha.setEnabled(readConfigBool("haEnabledParam"));

  mqttha.setThingName(readConfigValue("thingName", "esp-eBus").c_str());
  mqttha.setThingModel(ESP.getChipModel());
  mqttha.setThingModelId("Revision: " + std::to_string(ESP.getChipRevision()));
  mqttha.setThingManufacturer("danman.eu");
  mqttha.setThingSwVersion(AUTO_VERSION);
  mqttha.setThingHwVersion(adapterHwVersion);
  mqttha.setThingConfigurationUrl(
      "http://" + std::string(WiFi.localIP().toString().c_str()) + "/");
#endif

#if !defined(EBUS_INTERNAL)
  wifiServer.begin();
  wifiServerEnhanced.begin();
  wifiServerReadOnly.begin();
#endif

  statusServer.begin();

  upgradeManager.beginEspOta();
  MDNS.begin(HOSTNAME);
  wdt_start();

  last_comms = millis();
  enableTX();

#if defined(EBUS_INTERNAL)
  String ebusAddress = readConfigValue("ebusAddress", "ff");
  ebus::handler->setSourceAddress(
      uint8_t(std::strtoul(ebusAddress.c_str(), nullptr, 16)));
  ebus::setBusIsrWindow(configManager.readInt("busisrWindow", 4300));
  ebus::setBusIsrOffset(configManager.readInt("busisrOffset", 80));

  deviceManager.setEbusHandler(ebus::handler);
  deviceManager.setScanOnStartup(readConfigBool("scanOnStartPrm"));

  schedule.setSendInquiryOfExistence(readConfigBool("inquiryExistPrm"));
  schedule.setFirstCommandAfterStart(
      configManager.readInt("firstCmdAfterSt", 10));
  schedule.setPublishCounter(readConfigBool("mqttPublishCnt"));
  schedule.setPublishTiming(readConfigBool("mqttPublishTmg"));
  schedule.start(ebus::request, ebus::handler);

  ebus::serviceRunner->start();

  clientManager.setLastCommsCallback(updateLastComms);
  clientManager.start(ebus::bus, ebus::request, ebus::serviceRunner);

  store.setDataUpdatedCallback(Mqtt::publishValue);
  store.setDataUpdatedLogCallback(
      [](const String& message) { logger.debug(message); });
  store.loadCommands();  // install saved commands
  mqttha.publishComponents();
#else
  xTaskCreate(data_loop, "data_loop", 10000, NULL, 1, &Task1);
#endif
}

void loop() {
  wdt_feed();

  configServer.handleClient();

#if defined(EBUS_INTERNAL)
  if (mqtt.isEnabled()) {
    if (mqtt.isConnected()) {
      uint32_t currentMillis = millis();
      if (currentMillis > lastMqttUpdate + 10 * 1000) {
        lastMqttUpdate = currentMillis;

        mqtt.publish("state", 0, false, getMqttStatusJson().c_str());

        schedule.publishCounter();
        schedule.publishTiming();
      }
      mqtt.doLoop();
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
