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

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ebus/detail/json_writer.hpp>

#include "logger.hpp"

#if defined(EBUS_INTERNAL)
#include "cron.hpp"
#include "ebus_accessor.hpp"
#include "mqtt.hpp"
#include "mqtt_ha.hpp"
#include "store.hpp"
#else
#include "bus_type.hpp"
#include "client.hpp"
#endif

#include "adapter_version.hpp"
#include "config_manager.hpp"
#include "dns_server.hpp"
#include "esp_ota_manager.hpp"
#include "esp_rom_sys.h"
#include "esp_sntp.h"
#include "http.hpp"
#include "http_utils.hpp"
#include "upgrade_manager.hpp"
#include "wifi_network_manager.hpp"

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

// status
uint32_t reset_code = 0;

struct StatusInfo {
  static void toJson(ebus::detail::JsonWriter& writer) {
    auto scope = writer.objectScope();
    writer.writeField("Reset_Code", reset_code);
    writer.writeField("Uptime",
                      static_cast<uint32_t>(esp_timer_get_time() / 1000ULL));
  }
};

struct HeapStatus {
  static void toJson(ebus::detail::JsonWriter& writer) {
    auto scope = writer.objectScope();
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    writer.writeField("Total_Free_Bytes", info.total_free_bytes);
    writer.writeField("Largest_Free_Block", info.largest_free_block);
    writer.writeField("Minimum_Free_Bytes", info.minimum_free_bytes);
    writer.writeField("Free_Blocks", info.free_blocks);
    writer.writeField("Total_Blocks", info.total_blocks);
  }
};

#if !defined(EBUS_INTERNAL)
struct ArbitrationInfo {
  static void toJson(ebus::detail::JsonWriter& writer) {
    auto scope = writer.objectScope();
    writer.writeField("Total", static_cast<int>(Bus.nbr_arbitrations_));
    writer.writeField("Restarts1", static_cast<int>(Bus.nbr_restarts_1_));
    writer.writeField("Restarts2", static_cast<int>(Bus.nbr_restarts_2_));
    writer.writeField("Won1", static_cast<int>(Bus.nbr_won_1_));
    writer.writeField("Won2", static_cast<int>(Bus.nbr_won_2_));
    writer.writeField("Lost1", static_cast<int>(Bus.nbr_lost_1_));
    writer.writeField("Lost2", static_cast<int>(Bus.nbr_lost_2_));
    writer.writeField("Late", static_cast<int>(Bus.nbr_late_));
    writer.writeField("Errors", static_cast<int>(Bus.nbr_errors_));
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
  static void toJson(ebus::detail::JsonWriter& writer) {
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
  static void toJson(ebus::detail::JsonWriter& writer) {
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
  static void toJson(ebus::detail::JsonWriter& writer) {
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

constexpr uint16_t captive_dns_port = 53;
constexpr const char* captive_dns_ip_string = "192.168.4.1";
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

constexpr ledc_channel_t pwm_channel = LEDC_CHANNEL_0;
constexpr ledc_timer_t pwm_timer = LEDC_TIMER_0;
constexpr ledc_mode_t pwm_speed_mode = LEDC_LOW_SPEED_MODE;

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
  timer.speed_mode = pwm_speed_mode;
  timer.timer_num = pwm_timer;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.freq_hz = PWM_FREQ;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer);

  ledc_channel_config_t channel{};
  channel.speed_mode = pwm_speed_mode;
  channel.channel = pwm_channel;
  channel.timer_sel = pwm_timer;
  channel.gpio_num = PWM_PIN;
  channel.duty = 0;
  channel.hpoint = 0;
  ledc_channel_config(&channel);
#endif
}

void startCaptiveDns() {
  if (captiveDnsServer.start(captive_dns_port, "*", kCaptiveDnsIp)) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Captive DNS started on %s",
             captive_dns_ip_string);
    logger.info(buf);
    return;
  }

  logger.warn("Captive DNS start failed");
}

void prepareRuntimeForUpgrade() {
#if defined(EBUS_INTERNAL)
  cron.stop();
  if (mqtt.getTaskHandle() != nullptr) {  // Only stop if task is running
    mqtt.stopTask();
  }
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
#if defined(PWM_PIN)
  int value = configManager.readInt("pwmValue", 130);
  ledc_set_duty(pwm_speed_mode, pwm_channel, value);
  ledc_update_duty(pwm_speed_mode, pwm_channel);
#if defined(EBUS_INTERNAL)
  getEbusController().resetMetrics();
#endif
#endif
}

uint32_t get_pwm() {
#if defined(PWM_PIN)
  return ledc_get_duty(pwm_speed_mode, pwm_channel);
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
  char buf[128];
  const char* activeServer = esp_sntp_getservername(0);  // This can return NULL
  snprintf(buf, sizeof(buf), "SNTP synchronized to %s",
           (activeServer != nullptr ? activeServer : "unknown"));
  logger.info(buf);
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
  esp_sntp_setservername(
      0, sntpServerStorage.c_str());  // This expects a non-null c_str()

  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  esp_sntp_init();
  char buf[128];
  snprintf(buf, sizeof(buf), "SNTP started with server %s",
           sntpServerStorage.c_str());
  logger.info(buf);
}

void setTimezone(const char* timezone) {
  if (timezone != nullptr && strlen(timezone) > 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Timezone set to %s", timezone);
    logger.info(buf);
    setenv("TZ", timezone, 1);
    tzset();
  }
}

void fetchMqttStatus(const ebus::JsonChunkVisitor& visitor) {
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

void fetchAppStatus(const ebus::JsonChunkVisitor& visitor) {
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
    addThread("sim", simTaskHandle(), 2048);
#endif
    addThread("mqtt", mqtt.getTaskHandle(), 5120);
    addThread("cron", cron.getTaskHandle(), 1024);
    addThread("logger", logger.getTaskHandle(), 3072);
    addThread("dns", captiveDnsServer.getTaskHandle(), 2048);
    addThread("espota", espOtaManager.getTaskHandle(), 8192);
    addThread("status_led", WifiNetworkManager::getStatusLedTaskHandle(), 1024);
    addThread("socket_logger", WifiNetworkManager::getSocketLoggerTaskHandle(),
              3072);
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
  getEbusController().setSystemInquiry(configManager.readBool("systemInquiry"));
  getEbusController().setSystemResponse(
      configManager.readBool("systemResponse"));

  getEbusController().setWindow(configManager.readInt("busWindow", 4300));
  getEbusController().setOffset(configManager.readInt("busOffset", 80));

  getEbusController().setScanOnStartup(configManager.readBool("scanOnStartup"));

  if (configManager.readBool("sntpEnabled")) {
    esp_sntp_stop();
    initSNTP(
        configManager.readString("sntpServer", DEFAULT_SNTP_SERVER).c_str());
    setTimezone(configManager.readString("sntpTimezone", DEFAULT_SNTP_TIMEZONE)
                    .c_str());
  } else {
    esp_sntp_stop();
  }

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

  mqttha.setEnabled(configManager.readBool("haEnabledParam"));
  Mqtt::publishDiscovery();
  Mqtt::publishComponentDiscovery();
#endif
}

void fetchStatus(const ebus::JsonChunkVisitor& visitor) {
  ebus::detail::JsonWriter writer(visitor);
  auto scope = writer.objectScope();
  writer.writeField("Status", StatusInfo{});
  writer.writeField("Heap", HeapStatus{});

#if !defined(EBUS_INTERNAL)
  writer.writeField("Arbitration", ArbitrationInfo{});
#endif
  writer.writeField("Firmware", FirmwareStatus{});
  writer.writeField("Chip", ChipStatus{});
  writer.writeField("WIFI", WifiStatus{});

#if defined(EBUS_INTERNAL)
  writer.writeField("SNTP", SntpStatus{});

  struct EbusStatus {
    static void toJson(ebus::detail::JsonWriter& w) {
      auto obj_scope = w.objectScope();
      w.writeField("PWM", get_pwm());
      w.writeField("Ebus_Address",
                   configManager.readString("ebusAddress", "ff"));
      w.writeField("Bus_Window", configManager.readInt("busWindow", 4300));
      w.writeField("Bus_Offset", configManager.readInt("busOffset", 80));
      w.writeField("System_Inquiry", configManager.readBool("systemInquiry"));
      w.writeField("system_Response", configManager.readBool("systemResponse"));
    }
  };
  writer.writeField("eBUS", EbusStatus{});

  struct ScheduleStatus {
    static void toJson(ebus::detail::JsonWriter& w) {
      auto obj_scope = w.objectScope();
      w.writeField("Scan_On_Startup", configManager.readBool("scanOnStartup"));
      w.writeField("Active_Commands",
                   static_cast<uint32_t>(store.getActiveCommands()));
      w.writeField("Passive_Commands",
                   static_cast<uint32_t>(store.getPassiveCommands()));
    }
  };
  writer.writeField("Schedule", ScheduleStatus{});

  struct MqttStatus {
    static void toJson(ebus::detail::JsonWriter& w) {
      auto obj_scope = w.objectScope();
      w.writeField("Enabled", mqtt.isEnabled());
      w.writeField("Server", configManager.readString("mqttServer"));
      w.writeField("User", configManager.readString("mqttUser"));
      w.writeField("Connected", mqtt.isConnected());
    }
  };
  writer.writeField("MQTT", MqttStatus{});

  struct HaStatus {
    static void toJson(ebus::detail::JsonWriter& w) {
      auto obj_scope = w.objectScope();
      w.writeField("Enabled", mqttha.isEnabled());
    }
  };
  writer.writeField("Home_Assistant", HaStatus{});
#endif
}

void heap_caps_alloc_failed_hook(size_t requested_size, uint32_t caps,
                                 const char* function_name) {
  printf(
      "%s was called but failed to allocate %zu bytes with 0x%X "
      "capabilities.\n",
      function_name, requested_size, (int)caps);
}

extern "C" void app_main(void) {
  DebugSer.begin(115200);
  DebugSer.setDebugOutput(true);

  // esp_err_t error =
  //     heap_caps_register_failed_alloc_callback(heap_caps_alloc_failed_hook);

  // void* ptr = heap_caps_malloc(allocation_size, MALLOC_CAP_DEFAULT);

  logger.info("Starting esp-ebus adapter version " AUTO_VERSION);

#if defined(EBUS_INTERNAL)
  // Connect library logger to app logger
  ebus::Controller::setLogSink([](ebus::LogLevel level, std::string_view msg) {
    char buf[max_msg_length];
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

  set_pwm();  // This calls configManager.readInt("pwmValue", 130);

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
  mqtt.setStatusProvider(fetchMqttStatus);

  mqttha.setUniqueId(mqtt.getUniqueId());
  mqttha.setRootTopic(mqtt.getRootTopic());
  mqttha.setWillTopic(mqtt.getWillTopic());
  mqttha.setEnabled(configManager.readBool("haEnabledParam"));

  mqttha.setThingName(configManager.readString("thingName", "esp-eBus"));
  mqttha.setThingHwVersion(getAdapterHwVersionString());
  mqttha.setThingModel("esp-eBus Adapter");
  mqttha.setThingModelId("esp-ebus-adapter");
  WifiNetworkManager::setStaIpAssignedCallback(
      [](const std::string& ipAddress) {
        if (ipAddress.empty()) return;

        mqttha.setThingConfigurationUrl("http://" + ipAddress + "/");

        if (mqttha.isEnabled()) {
          Mqtt::publishDiscovery();
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
  runtimeConfig.log_level = ebus::LogLevel::debug;

  runtimeConfig.address = 0x01;  // slave address 0x06
  runtimeConfig.lock_counter = 3;
  runtimeConfig.system_inquiry = false;
  runtimeConfig.system_response = false;

  // Bus
  runtimeConfig.bus.window_us = 4300;
  runtimeConfig.bus.offset_us = 80;
  runtimeConfig.bus.watchdog_timeout_ms = 250;
  runtimeConfig.bus.syn_gen = true;

  // Network
  runtimeConfig.network.session_timeout_ms = 2000;
  runtimeConfig.network.transmit_timeout_ms = 1000;
  runtimeConfig.network.outbound_buffer_size = 2048;
  runtimeConfig.network.enable_server = true;
  runtimeConfig.network.port_regular = 3333;
  runtimeConfig.network.port_readonly = 3334;
  runtimeConfig.network.port_enhanced = 3335;

  // Device
  runtimeConfig.device.scan_on_startup = false;
  runtimeConfig.device.initial_delay_s = 5;
  runtimeConfig.device.startup_interval_s = 25;
  runtimeConfig.device.max_startup_scans = 5;

  // Scheduler
  runtimeConfig.scheduler.max_attempts = 1;
  runtimeConfig.scheduler.base_backoff_ms = 100;
  runtimeConfig.scheduler.fsm_timeout_ms = 1000;
  runtimeConfig.scheduler.total_timeout_ms = 2000;

#else
  logger.info("Running in normal eBUS mode");

  // General
  ebus::RuntimeConfig runtimeConfig{};
  runtimeConfig.log_level = ebus::LogLevel::debug;

  runtimeConfig.address = uint8_t(std::strtoul(
      configManager.readString("ebusAddress", "ff").c_str(), nullptr, 16));
  runtimeConfig.lock_counter = 3;
  runtimeConfig.system_inquiry = configManager.readBool("systemInquiry");
  runtimeConfig.system_response = configManager.readBool("systemResponse");

  // Bus
  runtimeConfig.bus.window_us = configManager.readInt("busWindow", 4300);
  runtimeConfig.bus.offset_us = configManager.readInt("busOffset", 80);
  runtimeConfig.bus.watchdog_timeout_ms = 250;
  runtimeConfig.bus.syn_gen = false;

  // Network
  runtimeConfig.network.session_timeout_ms = 2000;
  runtimeConfig.network.transmit_timeout_ms = 1000;
  runtimeConfig.network.outbound_buffer_size = 2048;
  runtimeConfig.network.enable_server = true;
  runtimeConfig.network.port_regular = 3333;
  runtimeConfig.network.port_readonly = 3334;
  runtimeConfig.network.port_enhanced = 3335;

  // Device
  runtimeConfig.device.scan_on_startup =
      configManager.readBool("scanOnStartup", false);
  runtimeConfig.device.initial_delay_s = 5;
  runtimeConfig.device.startup_interval_s = 25;
  runtimeConfig.device.max_startup_scans = 5;

  // Scheduler
  runtimeConfig.scheduler.max_attempts = 1;
  runtimeConfig.scheduler.base_backoff_ms = 100;
  runtimeConfig.scheduler.fsm_timeout_ms = 1000;
  runtimeConfig.scheduler.total_timeout_ms = 2000;

  // BusConfig
  ebus::BusConfig busConfig = {.uart_port = UART_NUM_1,
                               .rx_pin = UART_RX,
                               .tx_pin = UART_TX,
                               .timer_group = 1,
                               .timer_idx = 0};
  getEbusConfig().bus = busConfig;
#endif
  getEbusConfig().runtime = runtimeConfig;

  // CRITICAL: Ensure configuration is applied or we will crash
  if (!getEbusController().configure(getEbusConfig())) {
    logger.error("eBUS: Global Configuration failed! Simulation may crash.");
  }

  // Optimized callbacks: Avoid heap-heavy JSON work inside library threads
  getEbusController().setProtocolCallback([](const ebus::ProtocolInfo& info) {
    char buf[128];
    if (info.is_error)
      snprintf(buf, sizeof(buf), "%s / %s -> '%s'",
               ebus::toString(info.master_view).c_str(),
               ebus::toString(info.slave_view).c_str(),
               ebus::toString(info.protocol_error));
    else
      snprintf(buf, sizeof(buf), "%s / %s",
               ebus::toString(info.master_view).c_str(),
               ebus::toString(info.slave_view).c_str());
    logger.info(buf);
    if (info.is_error) {
      Mqtt::publishError(info);

    } else {
      Command* target = nullptr;
      if (info.poll_id !=
          0) {  // If it's a polling item, find the command by poll_id
        target = store.findCommand(info.poll_id);
        logger.info("Found by poll_id");
      }
      // If target is nullptr, updateData will find matching commands by
      // master_view (passive)
      store.updateData(target, info.master_view, info.slave_view);
    }
  });

  // getEbusController().setTraceCallback([](const ebus::BusEventInfo& info) {
  //   logger.debug(ebus::toJson(info, 256));
  // });

  startEbus();  // This will start the ebus controller

#if defined(EBUS_SIMULATION)
  startEbusSimulation();
#endif

  store.setDataUpdatedCallback(Mqtt::publishValue);

  store.setDataUpdatedLogCallback([](std::string_view key) {
    // Now handled asynchronously within the Mqtt Update action
  });

  // Setup lifecycle listeners to keep ebusController in sync with the Store
  store.setCommandChangedCallback([](Command* cmd) {
    // Remove existing poll item if it was already registered
    if (cmd->getPollId() != 0) {
      char log_buf[128];
      snprintf(log_buf, sizeof(log_buf),
               "Releasing Poll ID %lu for key '%.*s' during command change.",
               (unsigned long)cmd->getPollId(), (int)cmd->getKey().size(),
               cmd->getKey().data());
      logger.warn(log_buf);
      getEbusController().removePollItem(cmd->getPollId());
      cmd->setPollId(0);
    }
    // Add new poll item if active and has a valid read command
    if (cmd->getActive() && !cmd->getReadCmd().empty()) {
      std::string_view key = cmd->getKey();
      uint32_t id = getEbusController().addPollItem(3, cmd->getReadCmd(),
                                                    cmd->getInterval() * 1000);
      char log_buf[128];
      snprintf(log_buf, sizeof(log_buf),
               "Re-registering Poll ID %lu for key '%.*s'", (unsigned long)id,
               (int)key.size(), key.data());
      logger.info(log_buf);
      cmd->setPollId(id);
    } else {
      char log_buf[128];
      snprintf(log_buf, sizeof(log_buf),
               "No valid poll item to register/update for command change on "
               "key '%.*s'",
               (int)cmd->getKey().size(), cmd->getKey().data());
      logger.info(log_buf);
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

#if defined(EBUS_INTERNAL)
  // Emergency recovery: uncomment to wipe commands.json on boot
  // std::remove("/littlefs/commands.json");
  // std::remove("/littlefs/commands.json.tmp");
#endif

  store.loadCommands();  // Automatically registers poll items via the callback

  cron.initFileSystem();  // This should be called before cron.loadRules()
  cron.loadRules();
  cron.start();

  mqtt.startTask();
#else
  if (!startClientRuntime()) {
    logger.error("Failed to start client runtime");
  }
#endif
  vTaskDelete(nullptr);
}
