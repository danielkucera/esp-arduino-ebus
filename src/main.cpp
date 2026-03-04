#include "main.hpp"

#include <WiFi.h>
#include <cJSON.h>
#include <esp_efuse.h>

#include "Logger.hpp"

#if defined(EBUS_INTERNAL)
#include <Ebus.h>

#include "ClientManager.hpp"
#include "DeviceManager.hpp"
#include "Mqtt.hpp"
#include "MqttHA.hpp"
#include "Schedule.hpp"
#include "Store.hpp"
#else
#include "BusType.hpp"
#include "client.hpp"
#endif

#include <esp_task_wdt.h>

#include "ConfigManager.hpp"
#include "EspOtaManager.hpp"
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
EspOtaManager espOtaManager;
WifiNetworkManager wifiNetworkManager;

// minimum time of reset pin
#define RESET_MS 1000

// PWM
#define PWM_CHANNEL 0
#define PWM_FREQ 10000
#define PWM_RESOLUTION 8

#define DEFAULT_SNTP_SERVER "pool.ntp.org"
#define DEFAULT_SNTP_TIMEZONE "UTC0"

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

void wdt_start() {
  esp_task_wdt_init(60, true);
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
  const char* activeServer = esp_sntp_getservername(0);
  logger.info("SNTP synchronized to " +
              String(activeServer != nullptr ? activeServer : "unknown"));
}

static String sntpServerStorage = DEFAULT_SNTP_SERVER;

void initSNTP(const char* server) {
  if (server != nullptr && strlen(server) > 0) {
    sntpServerStorage = server;
  } else {
    sntpServerStorage = DEFAULT_SNTP_SERVER;
  }

  sntp_set_sync_interval(1 * 60 * 60 * 1000UL);  // 1 hour

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, sntpServerStorage.c_str());

  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  esp_sntp_init();
  logger.info("SNTP started with server " + sntpServerStorage);
}

void setTimezone(const char* timezone) {
  if (strlen(timezone) > 0) {
    logger.info("Timezone set to " + String(timezone));
    setenv("TZ", timezone, 1);
    tzset();
  }
}

const std::string getMqttStatusJson() {
  cJSON* doc = cJSON_CreateObject();
  cJSON_AddNumberToObject(doc, "reset_code", reset_code);
  cJSON_AddNumberToObject(doc, "uptime", uptime);
  cJSON_AddNumberToObject(doc, "free_heap", free_heap);
  cJSON_AddNumberToObject(doc, "loop_duration", loopDuration);
  cJSON_AddNumberToObject(doc, "rssi", WiFi.RSSI());

  char* printed = cJSON_PrintUnformatted(doc);
  std::string payload = printed != nullptr ? printed : "{}";
  if (printed != nullptr) cJSON_free(printed);
  cJSON_Delete(doc);
  return payload;
}
#endif

void saveParamsCallback() {
  set_pwm();

#if defined(EBUS_INTERNAL)
  String ebusAddress = configManager.readString("ebusAddress", "ff");
  ebus::handler->setSourceAddress(
      uint8_t(std::strtoul(ebusAddress.c_str(), nullptr, 16)));
  ebus::setBusIsrWindow(configManager.readInt("busisrWindow", 4300));
  ebus::setBusIsrOffset(configManager.readInt("busisrOffset", 80));

  if (configManager.readBool("sntpEnabled")) {
    esp_sntp_stop();
    initSNTP(configManager.readString("sntpServer", DEFAULT_SNTP_SERVER).c_str());
    setTimezone(configManager.readString("sntpTimezone", DEFAULT_SNTP_TIMEZONE).c_str());
  } else {
    esp_sntp_stop();
  }

  deviceManager.setScanOnStartup(configManager.readBool("scanOnStartPrm"));

  schedule.setSendInquiryOfExistence(configManager.readBool("inquiryExistPrm"));
  schedule.setFirstCommandAfterStart(
      configManager.readInt("firstCmdAfterSt", 10));

  String mqttServerValue = configManager.readString("mqttServer");
  String mqttUserValue = configManager.readString("mqttUser");
  String mqttPassValue = configManager.readString("mqttPass");
  mqtt.setEnabled(configManager.readBool("mqttEnabled"));
  mqtt.setServer(mqttServerValue.c_str(), 1883);
  mqtt.setCredentials(mqttUserValue.c_str(), mqttPassValue.c_str());
  mqtt.change();

  schedule.setPublishCounter(configManager.readBool("mqttPublishCnt"));
  schedule.setPublishTiming(configManager.readBool("mqttPublishTmg"));

  mqttha.setEnabled(configManager.readBool("haEnabledParam"));
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
  pos += snprintf(status + pos, bufferSize - pos, "bssid: %s\n",
                  WiFi.BSSIDstr().c_str());
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
  String sntpTimezoneValue =
      configManager.readString("sntpTimezone", DEFAULT_SNTP_TIMEZONE);
  pos += snprintf(status + pos, bufferSize - pos, "sntpEnabled: %s\r\n",
                  configManager.readBool("sntpEnabled") ? "true" : "false");
  const char* activeSntpServer = esp_sntp_getservername(0);
  pos += snprintf(status + pos, bufferSize - pos, "sntpServer: %s\r\n",
                  activeSntpServer != nullptr ? activeSntpServer : "");
  pos += snprintf(status + pos, bufferSize - pos, "sntpTimezone: %s\r\n",
                  sntpTimezoneValue.c_str());
#endif

  pos +=
      snprintf(status + pos, bufferSize - pos, "pwm_value: %u\r\n", get_pwm());

#if defined(EBUS_INTERNAL)
  String ebusAddress = configManager.readString("ebusAddress", "ff");
  pos += snprintf(status + pos, bufferSize - pos, "ebus_address: %s\r\n",
                  ebusAddress.c_str());
  pos += snprintf(status + pos, bufferSize - pos, "busisr_window: %i us\r\n",
                  configManager.readInt("busisrWindow", 4300));
  pos += snprintf(status + pos, bufferSize - pos, "busisr_offset: %i us\r\n",
                  configManager.readInt("busisrOffset", 80));

  pos +=
      snprintf(status + pos, bufferSize - pos, "inquiry_of_existence: %s\r\n",
               configManager.readBool("inquiryExistPrm") ? "true" : "false");
  pos += snprintf(status + pos, bufferSize - pos, "scan_on_startup: %s\r\n",
                  configManager.readBool("scanOnStartPrm") ? "true" : "false");
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

  String mqttServerValue = configManager.readString("mqttServer");
  String mqttUserValue = configManager.readString("mqttUser");
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
  cJSON* doc = cJSON_CreateObject();
  cJSON* status = cJSON_AddObjectToObject(doc, "Status");
  cJSON_AddNumberToObject(status, "Reset_Code", reset_code);
  cJSON_AddNumberToObject(status, "Uptime", uptime);
  cJSON_AddNumberToObject(status, "Free_Heap", free_heap);
  cJSON_AddNumberToObject(status, "Loop_Duration", loopDuration);
  cJSON_AddNumberToObject(status, "Loop_Duration_Max", maxLoopDuration);

#if !defined(EBUS_INTERNAL)
  // Arbitration
  cJSON* arbitration = cJSON_AddObjectToObject(doc, "Arbitration");
  cJSON_AddNumberToObject(arbitration, "Total",
                          static_cast<int>(Bus._nbrArbitrations));
  cJSON_AddNumberToObject(arbitration, "Restarts1",
                          static_cast<int>(Bus._nbrRestarts1));
  cJSON_AddNumberToObject(arbitration, "Restarts2",
                          static_cast<int>(Bus._nbrRestarts2));
  cJSON_AddNumberToObject(arbitration, "Won1", static_cast<int>(Bus._nbrWon1));
  cJSON_AddNumberToObject(arbitration, "Won2", static_cast<int>(Bus._nbrWon2));
  cJSON_AddNumberToObject(arbitration, "Lost1",
                          static_cast<int>(Bus._nbrLost1));
  cJSON_AddNumberToObject(arbitration, "Lost2",
                          static_cast<int>(Bus._nbrLost2));
  cJSON_AddNumberToObject(arbitration, "Late", static_cast<int>(Bus._nbrLate));
  cJSON_AddNumberToObject(arbitration, "Errors",
                          static_cast<int>(Bus._nbrErrors));
#endif

  // Firmware
  cJSON* firmware = cJSON_AddObjectToObject(doc, "Firmware");
  cJSON_AddStringToObject(firmware, "Version", AUTO_VERSION);
  cJSON_AddStringToObject(firmware, "SDK", ESP.getSdkVersion());
#if !defined(EBUS_INTERNAL)
  cJSON_AddBoolToObject(firmware, "Async", USE_ASYNCHRONOUS ? true : false);
  cJSON_AddBoolToObject(firmware, "Software_Serial",
                        USE_SOFTWARE_SERIAL ? true : false);
#endif
  cJSON_AddStringToObject(firmware, "Unique_ID", unique_id);
  cJSON_AddStringToObject(firmware, "Adapter_HW_Version",
                          adapterHwVersion.c_str());
  cJSON_AddNumberToObject(firmware, "Adapter_HW_Version_Raw", adapterHwVersionRaw);
  cJSON_AddNumberToObject(firmware, "Clock_Speed", getCpuFrequencyMhz());
  cJSON_AddNumberToObject(firmware, "Apb_Speed", getApbFrequency());

  // Chip
  cJSON* chip = cJSON_AddObjectToObject(doc, "Chip");
  cJSON_AddStringToObject(chip, "Chip_Model", ESP.getChipModel());
  cJSON_AddNumberToObject(chip, "Chip_Revision", ESP.getChipRevision());
  cJSON_AddNumberToObject(chip, "Flash_Chip_Size", ESP.getFlashChipSize());
  cJSON_AddNumberToObject(chip, "Flash_Chip_Speed", ESP.getFlashChipSpeed());
  cJSON_AddNumberToObject(chip, "Flash_Chip_Mode", ESP.getFlashChipMode());

  // WIFI
  cJSON* wifi = cJSON_AddObjectToObject(doc, "WIFI");
  cJSON_AddNumberToObject(wifi, "Last_Connect",
                          wifiNetworkManager.getLastConnect());
  cJSON_AddNumberToObject(wifi, "Reconnect_Count",
                          wifiNetworkManager.getReconnectCount());
  cJSON_AddNumberToObject(wifi, "RSSI", WiFi.RSSI());

  if (wifiNetworkManager.isStaticIpEnabled()) {
    cJSON_AddBoolToObject(wifi, "Static_IP", true);
    cJSON_AddStringToObject(
        wifi, "IP_Address", wifiNetworkManager.getConfiguredIpAddress().c_str());
    cJSON_AddStringToObject(
        wifi, "Gateway", wifiNetworkManager.getConfiguredGateway().c_str());
    cJSON_AddStringToObject(
        wifi, "Netmask", wifiNetworkManager.getConfiguredNetmask().c_str());
    cJSON_AddStringToObject(
        wifi, "DNS1", wifiNetworkManager.getConfiguredDns1().c_str());
    cJSON_AddStringToObject(
        wifi, "DNS2", wifiNetworkManager.getConfiguredDns2().c_str());
  } else {
    cJSON_AddBoolToObject(wifi, "Static_IP", false);
    cJSON_AddStringToObject(wifi, "IP_Address", WiFi.localIP().toString().c_str());
    cJSON_AddStringToObject(wifi, "Gateway", WiFi.gatewayIP().toString().c_str());
    cJSON_AddStringToObject(wifi, "Netmask", WiFi.subnetMask().toString().c_str());
    cJSON_AddStringToObject(wifi, "DNS1", WiFi.dnsIP(0).toString().c_str());
    cJSON_AddStringToObject(wifi, "DNS2", WiFi.dnsIP(1).toString().c_str());
  }
  cJSON_AddStringToObject(wifi, "SSID", WiFi.SSID().c_str());
  cJSON_AddStringToObject(wifi, "BSSID", WiFi.BSSIDstr().c_str());
  cJSON_AddNumberToObject(wifi, "Channel", WiFi.channel());
  cJSON_AddStringToObject(wifi, "Hostname", WiFi.getHostname());
  cJSON_AddStringToObject(wifi, "MAC_Address", WiFi.macAddress().c_str());

// SNTP
#if defined(EBUS_INTERNAL)
  cJSON* sntp = cJSON_AddObjectToObject(doc, "SNTP");
  cJSON_AddBoolToObject(sntp, "Enabled", configManager.readBool("sntpEnabled"));
  const char* activeSntpServer = esp_sntp_getservername(0);
  if (activeSntpServer != nullptr) {
    cJSON_AddStringToObject(sntp, "Server", activeSntpServer);
  } else {
    cJSON_AddStringToObject(
        sntp, "Server",
        configManager.readString("sntpServer", DEFAULT_SNTP_SERVER).c_str());
  }
  cJSON_AddStringToObject(
      sntp, "Timezone",
      configManager.readString("sntpTimezone", DEFAULT_SNTP_TIMEZONE).c_str());
#endif

  // eBUS
  cJSON* ebus = cJSON_AddObjectToObject(doc, "eBUS");
  cJSON_AddNumberToObject(ebus, "PWM", get_pwm());
#if defined(EBUS_INTERNAL)
  cJSON_AddStringToObject(ebus, "Ebus_Address",
                          configManager.readString("ebusAddress", "ff").c_str());
  cJSON_AddNumberToObject(ebus, "BusIsr_Window",
                          configManager.readInt("busisrWindow", 4300));
  cJSON_AddNumberToObject(ebus, "BusIsr_Offset",
                          configManager.readInt("busisrOffset", 80));

  // Schedule
  cJSON* scheduleObj = cJSON_AddObjectToObject(doc, "Schedule");
  cJSON_AddBoolToObject(scheduleObj, "Inquiry_Of_Existence",
                        configManager.readBool("inquiryExistPrm"));
  cJSON_AddBoolToObject(scheduleObj, "Scan_On_Startup",
                        configManager.readBool("scanOnStartPrm"));
  cJSON_AddNumberToObject(scheduleObj, "First_Command_After_Start",
                          configManager.readInt("firstCmdAfterSt", 10));
  cJSON_AddNumberToObject(scheduleObj, "Active_Commands",
                          store.getActiveCommands());
  cJSON_AddNumberToObject(scheduleObj, "Passive_Commands",
                          store.getPassiveCommands());

  // MQTT
  cJSON* mqttObj = cJSON_AddObjectToObject(doc, "MQTT");
  cJSON_AddBoolToObject(mqttObj, "Enabled", mqtt.isEnabled());
  cJSON_AddStringToObject(
      mqttObj, "Server", configManager.readString("mqttServer").c_str());
  cJSON_AddStringToObject(
      mqttObj, "User", configManager.readString("mqttUser").c_str());
  cJSON_AddBoolToObject(mqttObj, "Connected", mqtt.isConnected());
  cJSON_AddBoolToObject(mqttObj, "Publish_Counter", schedule.getPublishCounter());
  cJSON_AddBoolToObject(mqttObj, "Publish_Timing", schedule.getPublishTiming());

  // HomeAssistant
  cJSON* homeAssistant = cJSON_AddObjectToObject(doc, "Home_Assistant");
  cJSON_AddBoolToObject(homeAssistant, "Enabled", mqttha.isEnabled());
#endif

  char* printed = cJSON_PrintUnformatted(doc);
  std::string payload = printed != nullptr ? printed : "{}";
  if (printed != nullptr) cJSON_free(printed);
  cJSON_Delete(doc);
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

  logger.info("Starting esp-ebus adapter version " AUTO_VERSION);

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
  espOtaManager.setPreUpgradeHook([]() {
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
  if (configManager.readBool("sntpEnabled")) {
    String sntpServerValue = configManager.readString("sntpServer", DEFAULT_SNTP_SERVER);
    String sntpTimezoneValue =
        configManager.readString("sntpTimezone", DEFAULT_SNTP_TIMEZONE);
    initSNTP(sntpServerValue.c_str());
    setTimezone(sntpTimezoneValue.c_str());
  }

  String mqttServerValue = configManager.readString("mqttServer");
  String mqttUserValue = configManager.readString("mqttUser");
  String mqttPassValue = configManager.readString("mqttPass");
  mqtt.setEnabled(configManager.readBool("mqttEnabled"));
  mqtt.setup(unique_id);
  mqtt.setServer(mqttServerValue.c_str(), 1883);
  mqtt.setCredentials(mqttUserValue.c_str(), mqttPassValue.c_str());
  mqtt.start();

  mqttha.setUniqueId(mqtt.getUniqueId());
  mqttha.setRootTopic(mqtt.getRootTopic());
  mqttha.setWillTopic(mqtt.getWillTopic());
  mqttha.setEnabled(configManager.readBool("haEnabledParam"));

  mqttha.setThingName(configManager.readString("thingName", "esp-eBus").c_str());
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

  espOtaManager.begin();
  wdt_start();

  last_comms = millis();
  enableTX();

#if defined(EBUS_INTERNAL)
  String ebusAddress = configManager.readString("ebusAddress", "ff");
  ebus::handler->setSourceAddress(
      uint8_t(std::strtoul(ebusAddress.c_str(), nullptr, 16)));
  ebus::setBusIsrWindow(configManager.readInt("busisrWindow", 4300));
  ebus::setBusIsrOffset(configManager.readInt("busisrOffset", 80));

  deviceManager.setEbusHandler(ebus::handler);
  deviceManager.setScanOnStartup(configManager.readBool("scanOnStartPrm"));

  schedule.setSendInquiryOfExistence(configManager.readBool("inquiryExistPrm"));
  schedule.setFirstCommandAfterStart(
      configManager.readInt("firstCmdAfterSt", 10));
  schedule.setPublishCounter(configManager.readBool("mqttPublishCnt"));
  schedule.setPublishTiming(configManager.readBool("mqttPublishTmg"));
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
