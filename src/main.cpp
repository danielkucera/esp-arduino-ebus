#include "main.hpp"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
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
#include <ebus/detail/json_writer.hpp>

#include "Logger.hpp"

#if defined(EBUS_INTERNAL)
#include "Cron.hpp"
#include "Mqtt.hpp"
#include "MqttHA.hpp"
#include "Store.hpp"
#include "client_acceptor.hpp"
#include "ebus_accessor.hpp"
#else
#include "BusType.hpp"
#include "client.hpp"
#endif

#include "AdapterVersion.hpp"
#include "ConfigManager.hpp"
#include "DNSServer.h"
#include "EspOtaManager.hpp"
#include "HttpUtils.hpp"
#include "UpgradeManager.hpp"
#include "WifiNetworkManager.hpp"
#include "esp_rom_sys.h"
#include "esp_sntp.h"
#include "http.hpp"

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

#if defined(EBUS_SIMULATION)
TaskHandle_t simTaskHandle = nullptr;
#endif

char unique_id[7]{};

namespace {

// status
uint32_t reset_code = 0;

struct StatusInfo {
  void toJson(ebus::detail::JsonWriter& writer) const {
    auto scope = writer.objectScope();
    writer.writeField("Reset_Code", reset_code);
    writer.writeField("Uptime",
                      static_cast<uint32_t>(esp_timer_get_time() / 1000ULL));
    writer.writeField("Free_Heap", esp_get_free_heap_size());
  }
};

#if !defined(EBUS_INTERNAL)
struct ArbitrationInfo {
  void toJson(ebus::detail::JsonWriter& writer) const {
    auto scope = writer.objectScope();
    writer.writeField("Total", static_cast<int>(Bus._nbrArbitrations));
    writer.writeField("Restarts1", static_cast<int>(Bus._nbrRestarts1));
    writer.writeField("Restarts2", static_cast<int>(Bus._nbrRestarts2));
    writer.writeField("Won1", static_cast<int>(Bus._nbrWon1));
    writer.writeField("Won2", static_cast<int>(Bus._nbrWon2));
    writer.writeField("Lost1", static_cast<int>(Bus._nbrLost1));
    writer.writeField("Lost2", static_cast<int>(Bus._nbrLost2));
    writer.writeField("Late", static_cast<int>(Bus._nbrLate));
    writer.writeField("Errors", static_cast<int>(Bus._nbrErrors));
  }
};
#endif

struct FirmwareStatus {
  void toJson(ebus::detail::JsonWriter& writer) const {
    auto scope = writer.objectScope();
    writer.writeField("Version", AUTO_VERSION);
    writer.writeField("SDK", esp_get_idf_version());
#if !defined(EBUS_INTERNAL)
    writer.writeField("Async", static_cast<bool>(USE_ASYNCHRONOUS));
    writer.writeField("Software_Serial",
                      static_cast<bool>(USE_SOFTWARE_SERIAL));
#endif
    writer.writeField("Unique_ID", unique_id);
    writer.writeField("Adapter_HW_Version", getAdapterHwVersionString());
    writer.writeField("Adapter_HW_Version_Raw", getAdapterHwVersionRaw());
    writer.writeField("Clock_Speed", esp_clk_cpu_freq() / 1000000U);
    writer.writeField("Apb_Speed", esp_clk_apb_freq());
  }
};

struct ChipStatus {
  void toJson(ebus::detail::JsonWriter& writer) const {
    auto scope = writer.objectScope();
    esp_chip_info_t chip_info{};
    esp_chip_info(&chip_info);
    uint32_t flash_size = 0;
    if (esp_flash_default_chip != nullptr)
      esp_flash_get_size(esp_flash_default_chip, &flash_size);
    writer.writeField("Chip_Revision", static_cast<int>(chip_info.revision));
    writer.writeField("Flash_Chip_Size", flash_size);
  }
};

struct WifiStatus {
  void toJson(ebus::detail::JsonWriter& writer) const {
    auto scope = writer.objectScope();
    writer.writeField("Last_Connect", WifiNetworkManager::getLastConnect());
    writer.writeField("Reconnect_Count",
                      WifiNetworkManager::getReconnectCount());
    writer.writeField("RSSI", WifiNetworkManager::RSSI());
    if (WifiNetworkManager::isStaticIpEnabled()) {
      writer.writeField("Static_IP", true);
      writer.writeField("IP_Address",
                        WifiNetworkManager::getConfiguredIpAddress());
      writer.writeField("Gateway", WifiNetworkManager::getConfiguredGateway());
      writer.writeField("Netmask", WifiNetworkManager::getConfiguredNetmask());
      writer.writeField("DNS1", WifiNetworkManager::getConfiguredDns1());
      writer.writeField("DNS2", WifiNetworkManager::getConfiguredDns2());
    } else {
      esp_netif_ip_info_t staIpInfo{};
      const bool hasStaIp = WifiNetworkManager::getStaIpInfo(&staIpInfo);
      esp_ip4_addr_t dnsMain{}, dnsBackup{};
      const bool hasDnsMain = WifiNetworkManager::getDnsIp(0, &dnsMain);
      const bool hasDnsBackup = WifiNetworkManager::getDnsIp(1, &dnsBackup);
      writer.writeField("Static_IP", false);
      writer.writeField(
          "IP_Address",
          hasStaIp ? WifiNetworkManager::ipToString(staIpInfo.ip) : "");
      writer.writeField(
          "Gateway",
          hasStaIp ? WifiNetworkManager::ipToString(staIpInfo.gw) : "");
      writer.writeField(
          "Netmask",
          hasStaIp ? WifiNetworkManager::ipToString(staIpInfo.netmask) : "");
      writer.writeField(
          "DNS1", hasDnsMain ? WifiNetworkManager::ipToString(dnsMain) : "");
      writer.writeField("DNS2", hasDnsBackup
                                    ? WifiNetworkManager::ipToString(dnsBackup)
                                    : "");
    }
    writer.writeField("SSID", WifiNetworkManager::SSID());
    writer.writeField("BSSID", WifiNetworkManager::BSSIDstr());
    writer.writeField("Channel", WifiNetworkManager::channel());
    writer.writeField("Hostname", WifiNetworkManager::getHostname());
    writer.writeField("MAC_Address", WifiNetworkManager::macAddress());
  }
};

#if defined(EBUS_INTERNAL)
struct SntpStatus {
  void toJson(ebus::detail::JsonWriter& writer) const {
    auto scope = writer.objectScope();
    writer.writeField("Enabled", configManager.readBool("sntpEnabled"));
    const char* activeSntpServer = esp_sntp_getservername(0);
    if (activeSntpServer != nullptr) {
      writer.writeField("Server", activeSntpServer);
    } else {
      writer.writeField("Server", configManager.readString(
                                      "sntpServer", DEFAULT_SNTP_SERVER));
    }
    writer.writeField("Timezone", configManager.readString(
                                      "sntpTimezone", DEFAULT_SNTP_TIMEZONE));
  }
};
#endif

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
  mqtt.stopTask();
  stopEbus();

  vTaskDelay(pdMS_TO_TICKS(50));
#else
  stopClientRuntime();
#endif
}

}  // namespace

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
// #if defined(EBUS_INTERNAL)
//   schedule.resetCounter();
//   schedule.resetTiming();
// #endif
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

void fetchMqttStatusJson(const ebus::JsonChunkVisitor& visitor) {
  const uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000ULL);
  ssize_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  ssize_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

  ebus::detail::JsonWriter writer(visitor);
  auto scope = writer.objectScope();
  writer.writeField("reset_code", reset_code);
  writer.writeField("uptime", uptime);
  writer.writeField("free_heap", static_cast<uint32_t>(free_heap));
  writer.writeField("min_free_heap", static_cast<uint32_t>(min_free_heap));
  writer.writeField("rssi", WifiNetworkManager::RSSI());
}

void fetchAppResourcesJson(const ebus::JsonChunkVisitor& visitor) {
  ebus::detail::JsonWriter writer(visitor);
  auto scope = writer.objectScope();

  auto addThread = [&](const char* name, TaskHandle_t handle,
                       uint32_t stack_size) {
    if (!handle) return;
    ebus::ThreadStatus ts(
        name, static_cast<int32_t>(stack_size),
        static_cast<int32_t>(uxTaskGetStackHighWaterMark(handle) *
                             sizeof(StackType_t)));
    writer.writeValue(ts);
  };

  writer.appendKey("threads");
  {
    auto array = writer.arrayScope();
#if defined(EBUS_SIMULATION)
    addThread("sim", simTaskHandle, 2048);
#endif
    addThread("mqtt", mqtt.getTaskHandle(), 4096);
    addThread("cron", cron.getTaskHandle(), 1024);
    addThread("logger", logger.getTaskHandle(), 1536);
    addThread("client_acceptor", client_acceptor.getTaskHandle(), 1536);
    addThread("dns", captiveDnsServer.getTaskHandle(), 2048);
    addThread("espota", espOtaManager.getTaskHandle(), 8192);
    addThread("status_led", WifiNetworkManager::getStatusLedTaskHandle(), 1024);
    addThread("socket_logger", WifiNetworkManager::getSocketLoggerTaskHandle(),
              2048);
  }

  writer.appendKey("queues");
  {
    auto array = writer.arrayScope();
    auto addQueue = [&](const char* qname, size_t size, size_t cap,
                        size_t max_size) {
      ebus::QueueStatus qs(qname, size, cap, max_size);
      writer.writeValue(qs);
    };

    addQueue("mqtt_out", mqtt.getOutgoingQueueSize(),
             mqtt.getOutgoingQueueCapacity(),
             mqtt.getOutgoingQueueHighWatermark());

    addQueue("logger", logger.getQueueSize(), 32,
             logger.getQueueHighWatermark());
  }
}
#endif

void saveParamsCallback() {
  set_pwm();

#if defined(EBUS_INTERNAL)
  std::string ebusAddress = configManager.readString("ebusAddress", "ff");
  getEbusController().setAddress(
      uint8_t(std::strtoul(ebusAddress.c_str(), nullptr, 16)));
  getEbusController().setWindow(configManager.readInt("busisrWindow", 4300));
  getEbusController().setOffset(configManager.readInt("busisrOffset", 80));

  if (configManager.readBool("sntpEnabled")) {
    esp_sntp_stop();
    initSNTP(
        configManager.readString("sntpServer", DEFAULT_SNTP_SERVER).c_str());
    setTimezone(configManager.readString("sntpTimezone", DEFAULT_SNTP_TIMEZONE)
                    .c_str());
  } else {
    esp_sntp_stop();
  }

  // deviceManager.setScanOnStartup(configManager.readBool("scanOnStartPrm"));

  // schedule.setSendInquiryOfExistence(configManager.readBool("inquiryExistPrm"));
  // schedule.setFirstCommandAfterStart(
  //     configManager.readInt("firstCmdAfterSt", 10));

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

  // schedule.setPublishCounter(configManager.readBool("mqttPublishCnt"));
  // schedule.setPublishTiming(configManager.readBool("mqttPublishTmg"));

  mqttha.setEnabled(configManager.readBool("haEnabledParam"));
  mqttha.publishDeviceInfo();
  mqttha.publishComponents();
#endif
}

void fetchStatusJson(const ebus::JsonChunkVisitor& visitor) {
  ebus::detail::JsonWriter writer(visitor);
  auto scope = writer.objectScope();
  writer.writeField("Status", StatusInfo{});

#if !defined(EBUS_INTERNAL)
  writer.writeField("Arbitration", ArbitrationInfo{});
#endif
  writer.writeField("Firmware", FirmwareStatus{});
  writer.writeField("Chip", ChipStatus{});
  writer.writeField("WIFI", WifiStatus{});

#if defined(EBUS_INTERNAL)
  writer.writeField("SNTP", SntpStatus{});

  struct EbusStatus {
    void toJson(ebus::detail::JsonWriter& w) const {
      auto scope = w.objectScope();
      w.writeField("PWM", get_pwm());
      w.writeField("Ebus_Address",
                   configManager.readString("ebusAddress", "ff"));
      w.writeField("BusIsr_Window",
                   configManager.readInt("busisrWindow", 4300));
      w.writeField("BusIsr_Offset", configManager.readInt("busisrOffset", 80));
    }
  };
  writer.writeField("eBUS", EbusStatus{});

  struct ScheduleStatus {
    void toJson(ebus::detail::JsonWriter& w) const {
      auto scope = w.objectScope();
      w.writeField("Inquiry_Of_Existence",
                   configManager.readBool("inquiryExistPrm"));
      w.writeField("Scan_On_Startup", configManager.readBool("scanOnStartPrm"));
      w.writeField("First_Command_After_Start",
                   configManager.readInt("firstCmdAfterSt", 10));
      w.writeField("Active_Commands",
                   static_cast<uint32_t>(store.getActiveCommands()));
      w.writeField("Passive_Commands",
                   static_cast<uint32_t>(store.getPassiveCommands()));
    }
  };
  writer.writeField("Schedule", ScheduleStatus{});

  struct MqttStatus {
    void toJson(ebus::detail::JsonWriter& w) const {
      auto scope = w.objectScope();
      w.writeField("Enabled", mqtt.isEnabled());
      w.writeField("Server", configManager.readString("mqttServer"));
      w.writeField("User", configManager.readString("mqttUser"));
      w.writeField("Connected", mqtt.isConnected());
    }
  };
  writer.writeField("MQTT", MqttStatus{});

  struct HaStatus {
    void toJson(ebus::detail::JsonWriter& w) const {
      auto scope = w.objectScope();
      w.writeField("Enabled", mqttha.isEnabled());
    }
  };
  writer.writeField("Home_Assistant", HaStatus{});
#endif
}

extern "C" void app_main(void) {
  DebugSer.begin(115200);
  DebugSer.setDebugOutput(true);

  logger.info("Starting esp-ebus adapter version " AUTO_VERSION);

#if defined(EBUS_INTERNAL)
  // Connect library logger to app logger
  ebus::Controller::setLogSink([](ebus::LogLevel level, std::string_view msg) {
    char buf[LOG_MSG_MAX_LEN];
    int n = snprintf(buf, sizeof(buf), "eBUS-Lib: %.*s", (int)msg.size(),
                     msg.data());
    if (n < 0) return;
    std::string_view out(buf, std::min((size_t)n, sizeof(buf) - 1));

    switch (level) {
      case ebus::LogLevel::error:
        logger.error(out);
        break;
      case ebus::LogLevel::info:
        logger.info(out);
        break;
      case ebus::LogLevel::debug:
        logger.debug(out);
        break;
      default:
        break;
    }
  });
#endif

  check_reset();

  reset_code = (uint32_t)esp_rom_get_reset_reason(0);

  calcUniqueId();
  loadAdapterHwVersionFromEfuse();
  if (getAdapterHwVersionRaw() ==
      static_cast<uint8_t>(AdapterHwVersionEfuse::V7_0)) {
    WifiNetworkManager::setStatusLedPin(5);
  } else {
    WifiNetworkManager::setStatusLedPin(3);
  }

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
  mqtt.setStatusProvider(fetchMqttStatusJson);

  mqttha.setUniqueId(mqtt.getUniqueId());
  mqttha.setRootTopic(mqtt.getRootTopic());
  mqttha.setWillTopic(mqtt.getWillTopic());
  mqttha.setEnabled(configManager.readBool("haEnabledParam"));

  mqttha.setThingName(
      configManager.readString("thingName", "esp-eBus").c_str());
  mqttha.setThingHwVersion(getAdapterHwVersionString());
  mqttha.setThingModel("esp-eBus Adapter");
  mqttha.setThingModelId("esp-ebus-adapter");
  WifiNetworkManager::setStaIpAssignedCallback(
      [](const std::string& ipAddress) {
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

#if defined(EBUS_SIMULATION)
  logger.info("Running in eBUS simulation mode");

  // RuntimeConfig
  ebus::RuntimeConfig runtimeConfig{};
  runtimeConfig.address = 0x01;  // slave address 0x06
  runtimeConfig.lock_counter = 3;
  runtimeConfig.system_inquiry = false;
  runtimeConfig.system_response = false;

  // Bus
  runtimeConfig.bus.window_us = 4300;
  runtimeConfig.bus.offset_us = 80;
  runtimeConfig.bus.watchdog_timeout_ms =
      1000;  // Allow more headroom under load
  runtimeConfig.bus.syn_gen = true;

  // Diagnostics
  runtimeConfig.diagnostics.level = ebus::LogLevel::info;
  runtimeConfig.diagnostics.log_size = 1;

  // Network
  runtimeConfig.network.session_timeout_ms = 500;
  runtimeConfig.network.transmit_timeout_ms = 250;
  runtimeConfig.network.outbound_buffer_size = 512;

  // Device
  runtimeConfig.device.scan_on_startup = true;
  runtimeConfig.device.initial_delay_s = 5;
  runtimeConfig.device.startup_interval_s = 25;
  runtimeConfig.device.max_startup_scans = 5;

  // Scheduler
  runtimeConfig.scheduler.max_send_attempts = 1;
  runtimeConfig.scheduler.base_backoff_ms = 100;
  runtimeConfig.scheduler.fsm_timeout_ms = 1000;
  runtimeConfig.scheduler.total_timeout_ms = 2000;
#else
  logger.info("Running in normal eBUS mode");

  // General
  ebus::RuntimeConfig runtimeConfig{};
  runtimeConfig.address = uint8_t(std::strtoul(
      configManager.readString("ebusAddress", "ff").c_str(), nullptr, 16));
  runtimeConfig.lock_counter = configManager.readInt("lockCounter", 3);
  runtimeConfig.system_inquiry = configManager.readBool("systemInquiry");
  runtimeConfig.system_response = configManager.readBool("systemResponse");

  // Bus
  runtimeConfig.bus.window_us = configManager.readInt("windowUs", 4300);
  runtimeConfig.bus.offset_us = configManager.readInt("offsetUs", 80);
  runtimeConfig.bus.watchdog_timeout_ms =
      configManager.readInt("watchdogTimeoutMs", 250);
  runtimeConfig.bus.syn_gen = configManager.readBool("synGen", true);

  // Logging
  runtimeConfig.diagnostics.level =
      static_cast<ebus::LogLevel>(configManager.readInt("logLevel", 1));
  int log_size = configManager.readInt("logSize", 5);
#if defined(EBUS_LOG_HISTORY_SIZE)
  if (log_size > EBUS_LOG_HISTORY_SIZE) log_size = EBUS_LOG_HISTORY_SIZE;
#endif
  runtimeConfig.diagnostics.log_size = log_size;

  // Network
  // runtimeConfig.network.session_timeout_ms =
  //     configManager.readInt("sessionTimeoutMs", 500);
  // runtimeConfig.network.transmit_timeout_ms =
  //     configManager.readInt("transmitTimeoutMs", 250);
  // runtimeConfig.network.outbound_buffer_size =
  //     configManager.readInt("outboundBufferSize", 4096);

  // Device
  runtimeConfig.device.scan_on_startup =
      configManager.readBool("scanOnStart", false);
  // runtimeConfig.scanner.initial_delay_s =
  //     configManager.readInt("initialDelayS", 5);
  // runtimeConfig.scanner.startup_interval_s =
  //     configManager.readInt("startupIntervalS", 60);
  // runtimeConfig.scanner.max_startup_scans =
  //     configManager.readInt("maxStartupScans", 5);

  // Scheduler
  runtimeConfig.scheduler.max_send_attempts =
      configManager.readInt("maxSendAttempts", 1);
  // runtimeConfig.scheduler.base_backoff_ms =
  //     configManager.readInt("baseBackoffMs", 100);
  // runtimeConfig.scheduler.fsm_timeout_ms =
  //     configManager.readInt("fsmTimeoutMs", 1000);
  // runtimeConfig.scheduler.total_timeout_ms =
  //     configManager.readInt("totalTimeoutMs", 2000);

  // BusConfig
  ebus::BusConfig busConfig = {.uart_port = UART_NUM_1,
                               .rx_pin = UART_RX,
                               .tx_pin = UART_TX,
                               .timer_group = 1,
                               .timer_idx = 0};
  getEbusConfig().bus = busConfig;
#endif
  getEbusConfig().runtime = runtimeConfig;

  // CRITICAL: Ensure configuration is applied or we will crash on null
  // virtual_bus
  if (!getEbusController().configure(getEbusConfig())) {
    logger.error("eBUS: Global Configuration failed! Simulation may crash.");
  }

  // Optimized callbacks: Avoid heap-heavy JSON work inside library threads
  getEbusController().setProtocolCallback([](const ebus::ProtocolInfo& info) {
    if (info.is_error) {
      Mqtt::publishError(info);
    } else {
      Command* target = nullptr;
      if (info.poll_id !=
          0) {  // If it's a polling item, find the command by poll_id
        target = store.findCommand(info.poll_id);
      }
      // If target is nullptr, updateData will find matching commands by
      // master_view (passive)
      store.updateData(target, info.master_view, info.slave_view);
    }
  });

  startEbus();  // This will start the ebus controller

#if defined(EBUS_SIMULATION)
  if (getEbusController().isConfigured()) {
    auto& vbus = getEbusController().getVirtualBus();
    // We mimic a Vaillant device reactions
    vbus.addSlaveReaction(0x01, "08070400", "0ab54d4f434b0001020304", 0, 0);
    vbus.addSlaveReaction(0x01, "08b5090124", "09003231313230383030", 0, 0);
    vbus.addSlaveReaction(0x01, "08b5090125", "09313030303930373030", 0, 0);
    vbus.addSlaveReaction(0x01, "08b5090126", "09303036303035313337", 0, 0);
    vbus.addSlaveReaction(0x01, "08b5090127", "094e3800000000000000", 0, 0);
    vbus.addSlaveReaction(0x01, "08b509030d0800", "039e0100", 0, 0);
    vbus.addSlaveReaction(0x01, "08b509030d1600", "03170700", 0, 0);
  }

  xTaskCreate(
      [](void*) {
        // Wait for controller to be fully initialized and running
        while (!getEbusController().isRunning()) {
          vTaskDelay(pdMS_TO_TICKS(100));
        }

        // Periodic injections to simulate device updates without master
        // requests
        uint32_t count17 = 0;
        uint32_t count25 = 0;
        TickType_t xLastWakeTime = xTaskGetTickCount();
        const TickType_t xFrequency = pdMS_TO_TICKS(1000);  // 1 second interval
        auto& vbus = getEbusController().getVirtualBus();

        for (;;) {
          vTaskDelayUntil(&xLastWakeTime, xFrequency);

          if (getEbusController().isRunning()) {
            if (++count17 >= 17) {
              count17 = 0;
              // Example: Broadcasting of an outside temperature of 9.25°C
              // * Master 0x10 -> Broadcast (0xfe),
              // * Vaillant Service (0xb5 0x16 0x03 0x01),
              // * Data: 9.25°C - DATA2B -> 0x40, 0x09
              vbus.injectMasterMessage(0x10, "feb51603014009");
            }

            if (++count25 >= 25) {
              count25 = 0;
              // Example: Broadcasting of a brine inlet temperature of 31.44°C
              // * Master 0x10 -> Slave (0x08)
              // * Vaillant Service (0xb5 0x09 0x03 0x29 0x0f 0x00),
              // * Data: 31.44°C - DATA2C -> 0xf7, 0x01
              vbus.injectMasterSlaveMessage(0x10, "08b50903290f00",
                                            "050f00f70100");
            }
          }
        }
      },
      "sim", 2048, nullptr, 1, &simTaskHandle);
#endif

  client_acceptor.start();

  store.setDataUpdatedCallback(Mqtt::publishValue);

  store.setDataUpdatedLogCallback([](const std::string& key) {
    // Now handled asynchronously within the Mqtt Update action
  });

  // Setup lifecycle listeners to keep ebusController in sync with the Store
  store.setCommandChangedCallback([](Command* cmd) {
    // Remove existing poll item if it was already registered
    if (cmd->getPollId() != 0) {
      getEbusController().removePollItem(cmd->getPollId());
      cmd->setPollId(0);
    }
    // Add new poll item if active and has a valid read command
    if (cmd->getActive() && !cmd->getReadCmd().empty()) {
      std::string key = cmd->getKey();  // Capture key by value for the lambda
      uint32_t id = getEbusController().addPollItem(3, cmd->getReadCmd(),
                                                    cmd->getInterval() * 1000);
      cmd->setPollId(id);
    }
  });

  store.setCommandRemovedCallback([](Command* cmd) {
    if (cmd->getPollId() != 0) {
      getEbusController().removePollItem(cmd->getPollId());
      cmd->setPollId(0);
    }
  });

  if (!store.initFileSystem()) {
    logger.error("LittleFS initialization failed");
  }
  store.loadCommands();  // Automatically registers poll items via the callback

  cron.initFileSystem();  // This should be called before cron.loadRules()
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
