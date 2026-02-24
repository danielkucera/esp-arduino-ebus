#include "main.hpp"

#include <cJSON.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_chip_info.h>
#include <esp_efuse.h>
#include <esp_flash.h>
#include <esp_idf_version.h>
#include <esp_mac.h>
#include <esp_private/esp_clk.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>

#include <cerrno>
#include <cstring>

#include "Logger.hpp"

#if defined(EBUS_INTERNAL)
#include <Ebus.h>

#include "Adc.hpp"
#include "ClientManager.hpp"
#include "Cron.hpp"
#include "DeviceManager.hpp"
#include "Mqtt.hpp"
#include "MqttHA.hpp"
#include "Schedule.hpp"
#include "Store.hpp"
#else
#include "BusType.hpp"
#include "Adc.hpp"
#include "client.hpp"
#endif

#include "ConfigManager.hpp"
#include "DNSServer.h"
#include "EspOtaManager.hpp"
#include "HttpUtils.hpp"
#include "UpgradeManager.hpp"
#include "WifiNetworkManager.hpp"
#include "esp32c3/rom/rtc.h"
#include "esp_sntp.h"
#include "http.hpp"

#if defined(EBUS_INTERNAL)
ebus::ebusConfig ebusConfig;
ebus::Controller ebusController;
#endif

ConfigManager configManager;
UpgradeManager upgradeManager;
EspOtaManager espOtaManager;

// minimum time of reset pin
#define RESET_MS 1000

// PWM
#define PWM_CHANNEL 0
#define PWM_FREQ 10000
#define PWM_RESOLUTION 8

#define DEFAULT_SNTP_SERVER "pool.ntp.org"
#define DEFAULT_SNTP_TIMEZONE "UTC0"

char unique_id[7]{};

namespace {

constexpr uint16_t kCaptiveDnsPort = 53;
constexpr const char* kCaptiveDnsIpString = "192.168.4.1";
const esp_ip4_addr_t kCaptiveDnsIp = {.addr = ESP_IP4TOADDR(192, 168, 4, 1)};

DNSServer captiveDnsServer;

uint64_t getEfuseMac() {
  uint8_t mac[6]{};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  uint64_t value = 0;
  for (int i = 0; i < 6; ++i) {
    value = (value << 8) | mac[i];
  }
  return value;
}

constexpr ledc_channel_t kPwmChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kPwmTimer = LEDC_TIMER_0;
constexpr ledc_mode_t kPwmSpeedMode = LEDC_LOW_SPEED_MODE;

void configureGpioInputPullup(int pin) {
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << pin;
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = GPIO_PULLUP_ENABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
}

void initPwm() {
#if defined(PWM_PIN)
  ledc_timer_config_t timer{};
  timer.speed_mode = kPwmSpeedMode;
  timer.timer_num = kPwmTimer;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.freq_hz = PWM_FREQ;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer);

  ledc_channel_config_t channel{};
  channel.speed_mode = kPwmSpeedMode;
  channel.channel = kPwmChannel;
  channel.timer_sel = kPwmTimer;
  channel.gpio_num = PWM_PIN;
  channel.duty = 0;
  channel.hpoint = 0;
  ledc_channel_config(&channel);
#endif
}

void startCaptiveDns() {
  if (captiveDnsServer.start(kCaptiveDnsPort, "*", kCaptiveDnsIp)) {
    logger.info(std::string("Captive DNS started on ") + kCaptiveDnsIpString);
    return;
  }

  logger.warn("Captive DNS start failed");
}

void prepareRuntimeForUpgrade() {
#if defined(EBUS_INTERNAL)
  cron.stop();
  schedule.stop();
  clientManager.stop();
  mqtt.stopTask();
  ebusController.stop();

  vTaskDelay(pdMS_TO_TICKS(50));
#else
  stopClientRuntime();
#endif

}

}  // namespace

// status
uint32_t reset_code = 0;

enum class AdapterHwVersionEfuse : uint8_t {
  PRE_7_0 = 0x00,
  V7_0 = 0x70,
};

static constexpr size_t ADAPTER_HW_VERSION_EFUSE_BITS = 8;
static constexpr size_t ADAPTER_HW_VERSION_EFUSE_OFFSET =
    248;  // BLOCK3 bit 248..255

static const esp_efuse_desc_t ADAPTER_HW_VERSION_EFUSE_DESC = {
    EFUSE_BLK3, ADAPTER_HW_VERSION_EFUSE_OFFSET, ADAPTER_HW_VERSION_EFUSE_BITS};
static const esp_efuse_desc_t* ADAPTER_HW_VERSION_EFUSE_FIELD[] = {
    &ADAPTER_HW_VERSION_EFUSE_DESC, nullptr};
uint8_t adapterHwVersionRaw = 0xEE;
std::string adapterHwVersion = "unread";

inline void disableTX() {
#if defined(TX_DISABLE_PIN)
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << TX_DISABLE_PIN;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
  gpio_set_level(static_cast<gpio_num_t>(TX_DISABLE_PIN), 1);
#endif
}

inline void enableTX() {
#if defined(TX_DISABLE_PIN)
  gpio_set_level(static_cast<gpio_num_t>(TX_DISABLE_PIN), 0);
#endif
}

void set_pwm() {
  int value = configManager.readInt("pwmValue", 130);
#if defined(PWM_PIN)
  ledc_set_duty(kPwmSpeedMode, kPwmChannel, value);
  ledc_update_duty(kPwmSpeedMode, kPwmChannel);
#if defined(EBUS_INTERNAL)
  schedule.resetCounter();
  schedule.resetTiming();
#endif
#endif
}

uint32_t get_pwm() {
#if defined(PWM_PIN)
  return ledc_get_duty(kPwmSpeedMode, kPwmChannel);
#else
  return 0;
#endif
}

void calcUniqueId() {
  const uint32_t id = static_cast<uint32_t>(getEfuseMac() & 0xFFFFFFULL);
  snprintf(unique_id, sizeof(unique_id), "%06" PRIx32, id);
}

std::string formatAdapterHwVersion(const uint8_t raw) {
  if (static_cast<AdapterHwVersionEfuse>(raw) ==
      AdapterHwVersionEfuse::PRE_7_0) {
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
  const esp_err_t err = esp_efuse_read_field_blob(
      ADAPTER_HW_VERSION_EFUSE_FIELD, &raw, ADAPTER_HW_VERSION_EFUSE_BITS);
  if (err != ESP_OK) {
    adapterHwVersionRaw = 0xEE;
    adapterHwVersion = "reading error";
    return;
  }

  adapterHwVersionRaw = raw;
  adapterHwVersion = formatAdapterHwVersion(raw);
  
  // Set status LED pin based on hardware version
  if (static_cast<AdapterHwVersionEfuse>(raw) == AdapterHwVersionEfuse::V7_0) {
    WifiNetworkManager::setStatusLedPin(5);
  } else {
    WifiNetworkManager::setStatusLedPin(3);
  }
}

void restart() {
  disableTX();
  esp_restart();
}

void check_reset() {
  // check if RESET_PIN being hold low and reset
  configureGpioInputPullup(RESET_PIN);
  uint32_t resetStart = (uint32_t)(esp_timer_get_time() / 1000ULL);
  while (gpio_get_level(static_cast<gpio_num_t>(RESET_PIN)) == 0) {
    if ((uint32_t)(esp_timer_get_time() / 1000ULL) > resetStart + RESET_MS) {
      configManager.resetConfig();
      restart();
    }
  }
}

#if defined(EBUS_INTERNAL)
void time_sync_notification_cb(struct timeval* tv) {
  const char* activeServer = esp_sntp_getservername(0);
  logger.info(std::string("SNTP synchronized to ") +
              (activeServer != nullptr ? activeServer : "unknown"));
}

static std::string sntpServerStorage = DEFAULT_SNTP_SERVER;

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
    logger.info(std::string("Timezone set to ") + timezone);
    setenv("TZ", timezone, 1);
    tzset();
  }
}

const std::string getMqttStatusJson() {
  const uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000ULL);
  const uint32_t free_heap = esp_get_free_heap_size();

  cJSON* doc = cJSON_CreateObject();
  cJSON_AddNumberToObject(doc, "reset_code", reset_code);
  cJSON_AddNumberToObject(doc, "uptime", uptime);
  cJSON_AddNumberToObject(doc, "free_heap", free_heap);
  cJSON_AddNumberToObject(doc, "rssi", WifiNetworkManager::RSSI());

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
  std::string ebusAddress = configManager.readString("ebusAddress", "ff");
  ebusController.setAddress(
      uint8_t(std::strtoul(ebusAddress.c_str(), nullptr, 16)));
  ebusController.setWindow(configManager.readInt("busisrWindow", 4300));
  ebusController.setOffset(configManager.readInt("busisrOffset", 80));

  if (configManager.readBool("sntpEnabled")) {
    esp_sntp_stop();
    initSNTP(
        configManager.readString("sntpServer", DEFAULT_SNTP_SERVER).c_str());
    setTimezone(configManager.readString("sntpTimezone", DEFAULT_SNTP_TIMEZONE)
                    .c_str());
  } else {
    esp_sntp_stop();
  }

  deviceManager.setScanOnStartup(configManager.readBool("scanOnStartPrm"));

  schedule.setSendInquiryOfExistence(configManager.readBool("inquiryExistPrm"));
  schedule.setFirstCommandAfterStart(
      configManager.readInt("firstCmdAfterSt", 10));

  std::string mqttServerValue = configManager.readString("mqttServer");
  std::string mqttUserValue = configManager.readString("mqttUser");
  std::string mqttPassValue = configManager.readString("mqttPass");
  std::string rootTopicValue = configManager.readString("rootTopic", "");
  mqtt.setEnabled(configManager.readBool("mqttEnabled"));
  mqtt.setServer(mqttServerValue.c_str(), 1883);
  mqtt.setCredentials(mqttUserValue.c_str(), mqttPassValue.c_str());
  if (!rootTopicValue.empty()) {
    mqtt.setRootTopic(rootTopicValue);
  }
  mqtt.change();

  schedule.setPublishCounter(configManager.readBool("mqttPublishCnt"));
  schedule.setPublishTiming(configManager.readBool("mqttPublishTmg"));

  mqttha.setEnabled(configManager.readBool("haEnabledParam"));
  mqttha.publishDeviceInfo();
  mqttha.publishComponents();
#endif
}

const std::string getStatusJson() {
  const uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000ULL);
  const uint32_t free_heap = esp_get_free_heap_size();

  cJSON* doc = cJSON_CreateObject();
  cJSON* status = cJSON_AddObjectToObject(doc, "Status");
  cJSON_AddNumberToObject(status, "Reset_Code", reset_code);
  cJSON_AddNumberToObject(status, "Uptime", uptime);
  cJSON_AddNumberToObject(status, "Free_Heap", free_heap);

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
  cJSON_AddStringToObject(firmware, "SDK", esp_get_idf_version());
#if !defined(EBUS_INTERNAL)
  cJSON_AddBoolToObject(firmware, "Async", USE_ASYNCHRONOUS ? true : false);
  cJSON_AddBoolToObject(firmware, "Software_Serial",
                        USE_SOFTWARE_SERIAL ? true : false);
#endif
  cJSON_AddStringToObject(firmware, "Unique_ID", unique_id);
  cJSON_AddStringToObject(firmware, "Adapter_HW_Version",
                          adapterHwVersion.c_str());
  cJSON_AddNumberToObject(firmware, "Adapter_HW_Version_Raw",
                          adapterHwVersionRaw);
  cJSON_AddNumberToObject(firmware, "Clock_Speed", esp_clk_cpu_freq() / 1000000U);
  cJSON_AddNumberToObject(firmware, "Apb_Speed", esp_clk_apb_freq());

  // Chip
  cJSON* chip = cJSON_AddObjectToObject(doc, "Chip");
  esp_chip_info_t chip_info_json{};
  esp_chip_info(&chip_info_json);
  uint32_t flash_size_json = 0;
  if (esp_flash_default_chip != nullptr) {
    esp_flash_get_size(esp_flash_default_chip, &flash_size_json);
  }
  cJSON_AddNumberToObject(chip, "Chip_Revision", chip_info_json.revision);
  cJSON_AddNumberToObject(chip, "Flash_Chip_Size", flash_size_json);

  // WIFI
  cJSON* wifi = cJSON_AddObjectToObject(doc, "WIFI");
  cJSON_AddNumberToObject(wifi, "Last_Connect",
                          WifiNetworkManager::getLastConnect());
  cJSON_AddNumberToObject(wifi, "Reconnect_Count",
                          WifiNetworkManager::getReconnectCount());
  cJSON_AddNumberToObject(wifi, "RSSI", WifiNetworkManager::RSSI());

  if (WifiNetworkManager::isStaticIpEnabled()) {
    cJSON_AddBoolToObject(wifi, "Static_IP", true);
    cJSON_AddStringToObject(
        wifi, "IP_Address",
        WifiNetworkManager::getConfiguredIpAddress().c_str());
    cJSON_AddStringToObject(wifi, "Gateway",
                            WifiNetworkManager::getConfiguredGateway().c_str());
    cJSON_AddStringToObject(wifi, "Netmask",
                            WifiNetworkManager::getConfiguredNetmask().c_str());
    cJSON_AddStringToObject(wifi, "DNS1",
                            WifiNetworkManager::getConfiguredDns1().c_str());
    cJSON_AddStringToObject(wifi, "DNS2",
                            WifiNetworkManager::getConfiguredDns2().c_str());
  } else {
    esp_netif_ip_info_t staIpInfo{};
    const bool hasStaIp = WifiNetworkManager::getStaIpInfo(&staIpInfo);
    esp_ip4_addr_t dnsMain{};
    const bool hasDnsMain = WifiNetworkManager::getDnsIp(0, &dnsMain);
    esp_ip4_addr_t dnsBackup{};
    const bool hasDnsBackup = WifiNetworkManager::getDnsIp(1, &dnsBackup);

    cJSON_AddBoolToObject(wifi, "Static_IP", false);
    cJSON_AddStringToObject(
      wifi, "IP_Address",
      hasStaIp ? WifiNetworkManager::ipToString(staIpInfo.ip).c_str() : "");
    cJSON_AddStringToObject(
      wifi, "Gateway",
      hasStaIp ? WifiNetworkManager::ipToString(staIpInfo.gw).c_str() : "");
    cJSON_AddStringToObject(
      wifi, "Netmask",
      hasStaIp ? WifiNetworkManager::ipToString(staIpInfo.netmask).c_str()
           : "");
    cJSON_AddStringToObject(
      wifi, "DNS1",
      hasDnsMain ? WifiNetworkManager::ipToString(dnsMain).c_str() : "");
    cJSON_AddStringToObject(
      wifi, "DNS2",
      hasDnsBackup ? WifiNetworkManager::ipToString(dnsBackup).c_str() : "");
  }
  cJSON_AddStringToObject(wifi, "SSID", WifiNetworkManager::SSID().c_str());
  cJSON_AddStringToObject(wifi, "BSSID", WifiNetworkManager::BSSIDstr().c_str());
  cJSON_AddNumberToObject(wifi, "Channel", WifiNetworkManager::channel());
  cJSON_AddStringToObject(wifi, "Hostname", WifiNetworkManager::getHostname());
  cJSON_AddStringToObject(wifi, "MAC_Address", WifiNetworkManager::macAddress().c_str());

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
  cJSON_AddStringToObject(
      ebus, "Ebus_Address",
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
  cJSON_AddStringToObject(mqttObj, "Server",
                          configManager.readString("mqttServer").c_str());
  cJSON_AddStringToObject(mqttObj, "User",
                          configManager.readString("mqttUser").c_str());
  cJSON_AddBoolToObject(mqttObj, "Connected", mqtt.isConnected());
  cJSON_AddBoolToObject(mqttObj, "Publish_Counter",
                        schedule.getPublishCounter());
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

extern "C" void app_main(void) {
  DebugSer.begin(115200);
  DebugSer.setDebugOutput(true);

  logger.info("Starting esp-ebus adapter version " AUTO_VERSION);

  check_reset();

  reset_code = rtc_get_reset_reason(0);

  calcUniqueId();
  loadAdapterHwVersionFromEfuse();

#if !defined(EBUS_INTERNAL)
  Bus.begin();
#endif

  disableTX();

#if defined(PWM_PIN)
  initPwm();
#endif

  WifiNetworkManager::begin(&configManager);
  startCaptiveDns();
  SetupHttpHandlers();
  configManager.begin();
  HttpUtils::setCustomHeaders(configManager.readString("httpHeaders", ""));
  upgradeManager.begin();
  SetupHttpFallbackHandlers();
  upgradeManager.setPreUpgradeHook(prepareRuntimeForUpgrade);
  espOtaManager.setPreUpgradeHook(prepareRuntimeForUpgrade);

  set_pwm();

#if defined(EBUS_INTERNAL)
  if (configManager.readBool("sntpEnabled")) {
    std::string sntpServerValue =
        configManager.readString("sntpServer", DEFAULT_SNTP_SERVER);
    std::string sntpTimezoneValue =
        configManager.readString("sntpTimezone", DEFAULT_SNTP_TIMEZONE);
    initSNTP(sntpServerValue.c_str());
    setTimezone(sntpTimezoneValue.c_str());
  }

  std::string mqttServerValue = configManager.readString("mqttServer");
  std::string mqttUserValue = configManager.readString("mqttUser");
  std::string mqttPassValue = configManager.readString("mqttPass");
  std::string rootTopicValue = configManager.readString("rootTopic", "");
  mqtt.setEnabled(configManager.readBool("mqttEnabled"));
  mqtt.setup(unique_id);
  mqtt.setServer(mqttServerValue.c_str(), 1883);
  mqtt.setCredentials(mqttUserValue.c_str(), mqttPassValue.c_str());
  if (!rootTopicValue.empty()) {
    mqtt.setRootTopic(rootTopicValue);
  }
  mqtt.start();
  mqtt.setStatusProvider([]() { return getMqttStatusJson(); });

  mqttha.setUniqueId(mqtt.getUniqueId());
  mqttha.setRootTopic(mqtt.getRootTopic());
  mqttha.setWillTopic(mqtt.getWillTopic());
  mqttha.setEnabled(configManager.readBool("haEnabledParam"));

  mqttha.setThingName(configManager.readString("thingName", "esp-eBus").c_str()); 
  mqttha.setThingHwVersion(adapterHwVersion);
  mqttha.setThingModel("esp-eBus Adapter");
  mqttha.setThingModelId("esp-ebus-adapter");
  WifiNetworkManager::setStaIpAssignedCallback([](const std::string& ipAddress) {
    if (ipAddress.empty()) return;

    mqttha.setThingConfigurationUrl("http://" + ipAddress + "/");

    if (mqttha.isEnabled()) {
      mqttha.publishDeviceInfo();
    }
  });
#endif

  espOtaManager.begin();
  enableTX();

#if defined(EBUS_INTERNAL)
  ebus::busConfig busConfig = {.uart_port = UART_NUM_1,
                               .rx_pin = UART_RX,
                               .tx_pin = UART_TX,
                               .timer_group = 1,
                               .timer_idx = 0};

  ebusConfig.address = uint8_t(std::strtoul(
      configManager.readString("ebusAddress", "ff").c_str(), nullptr, 16));
  ebusConfig.window = configManager.readInt("busisrWindow", 4300);
  ebusConfig.offset = configManager.readInt("busisrOffset", 80);
  ebusConfig.bus = busConfig;

  ebusController.configure(ebusConfig);

  ebusController.start();

  deviceManager.setEbusHandler(ebusController.getHandler());
  deviceManager.setScanOnStartup(configManager.readBool("scanOnStartPrm"));

  schedule.setSendInquiryOfExistence(configManager.readBool("inquiryExistPrm"));
  schedule.setFirstCommandAfterStart(
      configManager.readInt("firstCmdAfterSt", 10));
  schedule.setPublishCounter(configManager.readBool("mqttPublishCnt"));
  schedule.setPublishTiming(configManager.readBool("mqttPublishTmg"));
  schedule.start(ebusController.getBus(), ebusController.getRequest(),
                 ebusController.getHandler());

  clientManager.start(ebusController.getBus(), ebusController.getBusHandler(),
                      ebusController.getRequest());

  store.setDataUpdatedCallback(Mqtt::publishValue);
  store.setDataUpdatedLogCallback(
      [](const std::string& message) { logger.debug(message); });
  if (!store.initFileSystem()) {
    logger.error("LittleFS initialization failed");
  }
  store.loadCommands();  // install saved commands
  cron.initFileSystem();
  cron.loadRules();
  cron.start();
  mqttha.publishComponents();
  mqtt.startTask();
#else
  if (!startClientRuntime()) {
    logger.error("Failed to start client runtime");
  }
#endif
  vTaskDelete(nullptr);
}
