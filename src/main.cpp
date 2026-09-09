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

#include "app_limits.hpp"
#include "logger.hpp"

#if defined(EBUS_INTERNAL)
#include "command_manager.hpp"
#include "cron.hpp"
#include "ebus_accessor.hpp"
#include "mqtt.hpp"
#include "mqtt_ha.hpp"
#include "system_monitor.hpp"
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
    writer.writeField("reset_code", reset_code);
    writer.writeField("uptime",
                      static_cast<uint32_t>(esp_timer_get_time() / 1000ULL));
  }
};

struct HeapStatus {
  static void toJson(ebus::detail::JsonWriter& writer) {
    auto scope = writer.objectScope();
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    writer.writeField("total_free_bytes", info.total_free_bytes);
    writer.writeField("largest_free_block", info.largest_free_block);
    writer.writeField("minimum_free_bytes", info.minimum_free_bytes);
    writer.writeField("free_blocks", info.free_blocks);
    writer.writeField("total_blocks", info.total_blocks);
  }
};

#if !defined(EBUS_INTERNAL)
struct ArbitrationInfo {
  static void toJson(ebus::detail::JsonWriter& writer) {
    auto scope = writer.objectScope();
    writer.writeField("total", static_cast<int>(Bus.nbr_arbitrations_));
    writer.writeField("restarts1", static_cast<int>(Bus.nbr_restarts_1_));
    writer.writeField("restarts2", static_cast<int>(Bus.nbr_restarts_2_));
    writer.writeField("won1", static_cast<int>(Bus.nbr_won_1_));
    writer.writeField("won2", static_cast<int>(Bus.nbr_won_2_));
    writer.writeField("lost1", static_cast<int>(Bus.nbr_lost_1_));
    writer.writeField("lost2", static_cast<int>(Bus.nbr_lost_2_));
    writer.writeField("late", static_cast<int>(Bus.nbr_late_));
    writer.writeField("errors", static_cast<int>(Bus.nbr_errors_));
  }
};
#endif

struct FirmwareStatus {
  void toJson(ebus::detail::JsonWriter& writer) const {
    auto scope = writer.objectScope();
    writer.writeField("version", AUTO_VERSION);
    writer.writeField("esp_idf_version", esp_get_idf_version());
#if !defined(EBUS_INTERNAL)
    writer.writeField("async", static_cast<bool>(USE_ASYNCHRONOUS));
    writer.writeField("software_serial",
                      static_cast<bool>(USE_SOFTWARE_SERIAL));
#endif
    writer.writeField("unique_id", unique_id);
    writer.writeField("adapter_hw_version", getAdapterHwVersionString());
    writer.writeField("adapter_hw_version_raw", getAdapterHwVersionRaw());
    writer.writeField("clock_speed", esp_clk_cpu_freq() / 1000000U);
    writer.writeField("apb_speed", esp_clk_apb_freq());
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
    writer.writeField("chip_revision", static_cast<int>(chip_info.revision));
    writer.writeField("flash_size", flash_size);
  }
};

struct WifiStatus {
  static void toJson(ebus::detail::JsonWriter& writer) {
    auto scope = writer.objectScope();
    writer.writeField("last_connect", WifiNetworkManager::getLastConnect());
    writer.writeField("reconnect_count",
                      WifiNetworkManager::getReconnectCount());
    writer.writeField("rssi", WifiNetworkManager::RSSI());
    if (WifiNetworkManager::isStaticIpEnabled()) {
      writer.writeField("static_ip", true);
      writer.writeField("ip_address",
                        WifiNetworkManager::getConfiguredIpAddress());
      writer.writeField("gateway", WifiNetworkManager::getConfiguredGateway());
      writer.writeField("netmask", WifiNetworkManager::getConfiguredNetmask());
      writer.writeField("dns1", WifiNetworkManager::getConfiguredDns1());
      writer.writeField("dns2", WifiNetworkManager::getConfiguredDns2());
    } else {
      esp_netif_ip_info_t staIpInfo{};
      const bool hasStaIp = WifiNetworkManager::getStaIpInfo(&staIpInfo);
      esp_ip4_addr_t dnsMain{}, dnsBackup{};
      const bool hasDnsMain = WifiNetworkManager::getDnsIp(0, &dnsMain);
      const bool hasDnsBackup = WifiNetworkManager::getDnsIp(1, &dnsBackup);
      writer.writeField("static_ip", false);
      writer.writeField(
          "ip_address",
          hasStaIp ? WifiNetworkManager::ipToString(staIpInfo.ip) : "");
      writer.writeField(
          "gateway",
          hasStaIp ? WifiNetworkManager::ipToString(staIpInfo.gw) : "");
      writer.writeField(
          "netmask",
          hasStaIp ? WifiNetworkManager::ipToString(staIpInfo.netmask) : "");
      writer.writeField(
          "dns1", hasDnsMain ? WifiNetworkManager::ipToString(dnsMain) : "");
      writer.writeField("dns2", hasDnsBackup
                                    ? WifiNetworkManager::ipToString(dnsBackup)
                                    : "");
    }
    writer.writeField("ssid", WifiNetworkManager::SSID());
    writer.writeField("bssid", WifiNetworkManager::BSSIDstr());
    writer.writeField("channel", WifiNetworkManager::channel());
    writer.writeField("hostname", WifiNetworkManager::getHostname());
    writer.writeField("mac_address", WifiNetworkManager::macAddress());
  }
};

#if defined(EBUS_INTERNAL)
struct SntpStatus {
  static void toJson(ebus::detail::JsonWriter& writer) {
    auto scope = writer.objectScope();
    writer.writeField("enabled", configManager.readBool("sntpEnabled"));
    const char* activeSntpServer = esp_sntp_getservername(0);
    if (activeSntpServer != nullptr) {
      writer.writeField("server", activeSntpServer);
    } else {
      writer.writeField("server", configManager.readString(
                                      "sntpServer", DEFAULT_SNTP_SERVER));
    }
    writer.writeField("timezone", configManager.readString(
                                      "sntpTimezone", DEFAULT_SNTP_TIMEZONE));
  }
};

struct EbusStatus {
  static void toJson(ebus::detail::JsonWriter& w) {
    auto obj_scope = w.objectScope();
    w.writeField("pwm", configManager.readInt("pwmValue", 130));
    w.writeField("ebus_address", configManager.readString("ebusAddress", "ff"));
    w.writeField("bus_window", configManager.readInt("busWindow", 4400));
    w.writeField("bus_offset", configManager.readInt("busOffset", 50));
  }
};

struct ScheduleStatus {
  static void toJson(ebus::detail::JsonWriter& w) {
    auto obj_scope = w.objectScope();
    w.writeField("system_inquiry", configManager.readBool("systemInquiry"));
    w.writeField("system_response", configManager.readBool("systemResponse"));
    w.writeField("scan_on_startup", configManager.readBool("scanOnStartup"));
    w.writeField("active_commands",
                 static_cast<uint32_t>(commandManager.getActiveCommands()));
    w.writeField("passive_commands",
                 static_cast<uint32_t>(commandManager.getPassiveCommands()));
  }
};

struct MqttStatus {
  static void toJson(ebus::detail::JsonWriter& w) {
    auto obj_scope = w.objectScope();
    w.writeField("enabled", mqtt.isEnabled());
    w.writeField("server", configManager.readString("mqttServer"));
    w.writeField("user", configManager.readString("mqttUser"));
    w.writeField("connected", mqtt.isConnected());
  }
};

struct HaStatus {
  static void toJson(ebus::detail::JsonWriter& w) {
    auto obj_scope = w.objectScope();
    w.writeField("enabled", mqttha.isEnabled());
  }
};

struct SocketsStatus {
  static void toJson(ebus::detail::JsonWriter& w) {
    auto obj_scope = w.objectScope();
    int detected = 0;
    int connected = 0;
    SystemMonitor::getSocketStatus(detected, connected);
    w.writeField("detected", detected);
    w.writeField("connected", connected);
    w.writeField("max", CONFIG_LWIP_MAX_SOCKETS);
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
  // CRITICAL: Stop MQTT first and wait for task to fully exit
  // This prevents the MQTT task from accessing eBUS/Cron/SystemMonitor
  // resources while they are being stopped
  mqtt.stopTask();

  // Now safe to stop other components
  cron.stop();
  stopEbus();
  SystemMonitor::stop();

  vTaskDelay(pdMS_TO_TICKS(500));
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
    addThread("mqtt", mqtt.getTaskHandle(), app::limits::Task::mqtt_stack);
    addThread("cron", cron.getTaskHandle(), app::limits::Task::cron_stack);
    addThread("logger", logger.getTaskHandle(),
              app::limits::Task::logger_stack);
    addThread("dns", captiveDnsServer.getTaskHandle(),
              app::limits::Task::dns_stack);
    addThread("espota", espOtaManager.getTaskHandle(),
              app::limits::Task::espota_stack);
    addThread("status_led", WifiNetworkManager::getStatusLedTaskHandle(),
              app::limits::Task::status_led_stack);
    addThread("system_monitor", SystemMonitor::task_handle(),
              app::limits::Task::system_monitor_stack);
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

    addQueue("logger", logger.getQueueSize(), logger.getQueueCapacity(),
             logger.getQueueHighWatermark());

    addQueue("system_monitor_log", SystemMonitor::getLogQueueSize(),
             SystemMonitor::getLogQueueCapacity(),
             SystemMonitor::getLogQueueHighWatermark());

    addQueue("system_monitor_protocol", SystemMonitor::getProtocolQueueSize(),
             SystemMonitor::getProtocolQueueCapacity(),
             SystemMonitor::getProtocolQueueHighWatermark());
  }
}
#endif

void fetchStatus(const ebus::JsonChunkVisitor& visitor) {
  ebus::detail::JsonWriter writer(visitor);
  auto scope = writer.objectScope();
  writer.writeField("status", StatusInfo{});
  writer.writeField("heap", HeapStatus{});

#if !defined(EBUS_INTERNAL)
  writer.writeField("arbitration", ArbitrationInfo{});
#endif
  writer.writeField("firmware", FirmwareStatus{});
  writer.writeField("chip", ChipStatus{});
  writer.writeField("wifi", WifiStatus{});

#if defined(EBUS_INTERNAL)
  writer.writeField("sntp", SntpStatus{});
  writer.writeField("ebus", EbusStatus{});
  writer.writeField("schedule", ScheduleStatus{});
  writer.writeField("mqtt", MqttStatus{});
  writer.writeField("home_assistant", HaStatus{});
  writer.writeField("sockets", SocketsStatus{});
#endif
}

extern "C" void app_main(void) {
  DebugSer.begin(115200);
  DebugSer.setDebugOutput(true);

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
  HttpUtils::setCustomHeaders(
      std::string(configManager.readString("httpHeaders", "")));
  upgradeManager.begin();
  SetupHttpFallbackHandlers();
  upgradeManager.setPreUpgradeHook(prepareRuntimeForUpgrade);
  espOtaManager.setPreUpgradeHook(prepareRuntimeForUpgrade);

  set_pwm();  // This calls configManager.readInt("pwmValue", 130);

#if defined(EBUS_INTERNAL)
  if (configManager.readBool("sntpEnabled")) {
    std::string sntpServerValue = std::string(
        configManager.readString("sntpServer", DEFAULT_SNTP_SERVER));
    std::string sntpTimezoneValue = std::string(
        configManager.readString("sntpTimezone", DEFAULT_SNTP_TIMEZONE));
    initSNTP(sntpServerValue.c_str());
    setTimezone(sntpTimezoneValue.c_str());
  }

  std::string mqttServerValue =
      std::string(configManager.readString("mqttServer"));
  std::string mqttUserValue = std::string(configManager.readString("mqttUser"));
  std::string mqttPassValue = std::string(configManager.readString("mqttPass"));
  std::string rootTopicValue =
      std::string(configManager.readString("rootTopic", ""));
  mqtt.setEnabled(configManager.readBool("mqttEnabled"));
  mqtt.setup(unique_id);
  mqtt.setServer(mqttServerValue.c_str(), 1883);
  mqtt.setCredentials(mqttUserValue.c_str(), mqttPassValue.c_str());
  if (!rootTopicValue.empty()) {
    mqtt.setRootTopic(rootTopicValue);
  }
  mqtt.start();
  mqtt.setStatusProvider(fetchStatus);

  mqttha.setUniqueId(mqtt.getUniqueId());
  mqttha.setRootTopic(mqtt.getRootTopic());
  mqttha.setWillTopic(mqtt.getWillTopic());
  mqttha.setEnabled(configManager.readBool("haEnabled"));

  mqttha.setThingName(
      std::string(configManager.readString("thingName", "esp-eBus")));
  mqttha.setThingHwVersion(getAdapterHwVersionString());
  mqttha.setThingModel("esp-eBus Adapter");
  mqttha.setThingModelId("esp-ebus-adapter");
  WifiNetworkManager::setStaIpAssignedCallback(
      [](const std::string& ipAddress) {
        if (ipAddress.empty()) return;

        mqttha.setThingConfigurationUrl("http://" + ipAddress + "/");

        if (mqttha.isEnabled()) {
          Mqtt::publishDiscovery();
          Mqtt::publishComponentDiscovery();
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
  runtimeConfig.bus.window_us = configManager.readInt("busWindow", 4400);
  runtimeConfig.bus.offset_us = configManager.readInt("busOffset", 50);
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
      std::string(configManager.readString("ebusAddress", "ff")).c_str(),
      nullptr, 16));
  runtimeConfig.lock_counter = 3;
  runtimeConfig.system_inquiry = configManager.readBool("systemInquiry");
  runtimeConfig.system_response = configManager.readBool("systemResponse");

  // Bus
  runtimeConfig.bus.window_us = configManager.readInt("busWindow", 4400);
  runtimeConfig.bus.offset_us = configManager.readInt("busOffset", 50);
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
    logger.info(buf, false, info.session_id, info.poll_id);
    SystemMonitor::enqueueProtocolInfo(info);
  });

  // getEbusController().setTraceCallback([](const ebus::BusEventInfo& info) {
  //   logger.debug(ebus::toJson(info, 256));
  // });

  startEbus();  // This will start the ebus controller

#if defined(EBUS_SIMULATION)
  startEbusSimulation();
#endif

  SystemMonitor::begin();

  commandManager.setDataUpdatedCallback(Mqtt::publishValue);

  commandManager.setDataUpdatedLogCallback(
      [](std::string_view key) { SystemMonitor::enqueueLogRequest(key); });

  // Setup lifecycle listeners to keep ebusController in sync with the
  // CommandManager
  commandManager.setCommandChangedCallback([](Command* cmd) {
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
    // HA: publish components for this command if HA enabled and MQTT connected
    if (mqttha.isEnabled()) {
      for (size_t i = 0; i < cmd->getFieldCount(); ++i) {
        if (cmd->hasFieldHA(i)) {
          Mqtt::enqueueOutgoing(OutgoingAction(cmd, i, false));
        }
      }
    }
  });

  commandManager.setCommandRemovedCallback([](Command* cmd) {
    if (cmd->getPollId() != 0) {
      getEbusController().removePollItem(cmd->getPollId());
      cmd->setPollId(0);
    }
    if (mqttha.isEnabled()) {
      mqttha.removeComponent(cmd);
    }
  });

  if (!commandManager.initFileSystem()) {
    logger.error("LittleFS initialization failed");
  }

#if defined(EBUS_INTERNAL)
  // Emergency recovery: uncomment to wipe commands.json on boot
  // std::remove("/littlefs/commands.json");
  // std::remove("/littlefs/commands.json.tmp");
#endif

  commandManager
      .loadCommands();  // Automatically registers poll items via the callback

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
