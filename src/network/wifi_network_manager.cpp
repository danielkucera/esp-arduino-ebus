#include "network/wifi_network_manager.hpp"

#include <arpa/inet.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/ip4_addr.h>
#include <lwip/sockets.h>
#include <mdns.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

#include "app/app_limits.hpp"
#include "config/config_manager.hpp"
#include "network/detail/wifi.hpp"
#include "system/logger.hpp"

ConfigManager* WifiNetworkManager::configManager_ = nullptr;
esp_ip4_addr_t WifiNetworkManager::ipAddress_{};
esp_ip4_addr_t WifiNetworkManager::gateway_{};
esp_ip4_addr_t WifiNetworkManager::netmask_{};
esp_ip4_addr_t WifiNetworkManager::dns1_{};
esp_ip4_addr_t WifiNetworkManager::dns2_{};
uint32_t WifiNetworkManager::lastConnect_ = 0;
int WifiNetworkManager::reconnectCount_ = 0;
int WifiNetworkManager::consecutiveFailures_ = 0;
bool WifiNetworkManager::staConnected_ = false;
bool WifiNetworkManager::staConfigured_ = false;
TaskHandle_t WifiNetworkManager::statusLedTaskHandle_ = nullptr;
volatile WifiNetworkManager::StatusLedMode WifiNetworkManager::statusLedMode_ =
    WifiNetworkManager::StatusLedMode::SlowBlink;
int WifiNetworkManager::statusLedPin_ = -1;
void (*WifiNetworkManager::staIpAssignedCallback_)(
    const std::string& ipAddress) = nullptr;
esp_netif_t* WifiNetworkManager::staNetif_ = nullptr;
esp_netif_t* WifiNetworkManager::apNetif_ = nullptr;

namespace {

uint8_t consecutiveDisconnects = 0;
esp_timer_handle_t reconnectTimer = nullptr;
bool recoveryApConfigured = false;

void scheduleReconnect(uint64_t delayUs);

void reconnectTimerCallback(void*) {
  const esp_err_t result = esp_wifi_connect();
  if (result != ESP_OK) {
    logger.warn(std::string("STA reconnect failed: ") +
                esp_err_to_name(result));
    scheduleReconnect(5000000);
  }
}

void scheduleReconnect(uint64_t delayUs) {
  if (reconnectTimer == nullptr) return;
  esp_timer_stop(reconnectTimer);
  const esp_err_t result = esp_timer_start_once(reconnectTimer, delayUs);
  if (result != ESP_OK) {
    logger.warn(std::string("Failed to schedule STA reconnect: ") +
                esp_err_to_name(result));
  }
}

}  // namespace

bool WifiNetworkManager::begin(ConfigManager* configManager) {
  static constexpr const char* default_hostname = "esp-eBus";
  static constexpr const char* default_ap_ssid = "esp-eBus";
  static constexpr const char* default_ap_password = "ebusebus";
  static bool started = false;
  if (started) return getMode() != WIFI_MODE_NULL;
  started = true;

  configManager_ = configManager;
  initStatusLed();
  setStatusLedMode(StatusLedMode::SlowBlink);

  std::string apPassword = configManager_ != nullptr
                               ? std::string(configManager_->readString("apModePassword", default_ap_password))
                               : std::string(default_ap_password);
  if (apPassword.size() < 8 || apPassword.size() > 63) {
    apPassword = default_ap_password;
  }
  const std::string configuredThingName =
      configManager_ != nullptr ? std::string(configManager_->readString(
                                      "thingName", default_hostname))
                                : std::string(default_hostname);
  const std::string hostname = network::detail::wifi::buildHostname(
      configuredThingName, default_hostname);

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    logger.error("esp_netif_init failed");
    return false;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    logger.error("esp_event_loop_create_default failed");
    return false;
  }

  if (staNetif_ == nullptr) {
    staNetif_ = esp_netif_create_default_wifi_sta();
  }
  if (apNetif_ == nullptr) {
    apNetif_ = esp_netif_create_default_wifi_ap();
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) != ESP_OK) {
    logger.error("esp_wifi_init failed");
    return false;
  }

  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      &WifiNetworkManager::handle_event,
                                      nullptr, nullptr);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                      &WifiNetworkManager::handle_event,
                                      nullptr, nullptr);

  esp_wifi_set_storage(false ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM);

  esp_netif_set_hostname(staNetif_, hostname.c_str());

  if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
    logger.error("Failed to set WiFi mode");
    return false;
  }

  if (esp_wifi_start() != ESP_OK) {
    logger.error("Failed to start WiFi");
    return false;
  }

  if (reconnectTimer == nullptr) {
    const esp_timer_create_args_t reconnectTimerArgs = {
        .callback = reconnectTimerCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_reconnect",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&reconnectTimerArgs, &reconnectTimer) != ESP_OK) {
      logger.warn("Failed to create STA reconnect timer");
    }
  }

  // Initialize mDNS
  esp_err_t mdnsErr = mdns_init();
  if (mdnsErr != ESP_OK) {
    char buf[32];
    snprintf(buf, sizeof(buf), "mdns_init failed: %d", mdnsErr);
    logger.warn(buf);
  } else {
    mdns_hostname_set(hostname.c_str());
    mdns_instance_name_set(hostname.c_str());
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    logger.info("mDNS started: " + hostname + ".local");
  }

  wifi_config_t apConfig{};
  std::strncpy(reinterpret_cast<char*>(apConfig.ap.ssid), default_ap_ssid,
               sizeof(apConfig.ap.ssid) - 1);
  apConfig.ap.ssid_len = std::strlen(default_ap_ssid);
  std::strncpy(reinterpret_cast<char*>(apConfig.ap.password),
               apPassword.c_str(), sizeof(apConfig.ap.password) - 1);
  apConfig.ap.max_connection = 4;
  apConfig.ap.channel = rand() % 12 + 1;
  apConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
  if (apPassword.size() < 8) apConfig.ap.authmode = WIFI_AUTH_OPEN;
  if (esp_wifi_set_config(WIFI_IF_AP, &apConfig) != ESP_OK) {
    logger.error("AP config apply failed");
  } else {
    recoveryApConfigured = true;
    char buf[64];
    snprintf(buf, sizeof(buf), "AP ready: %s (%s)", default_ap_ssid,
             (apConfig.ap.authmode == WIFI_AUTH_OPEN ? "open" : "wpa2"));
    logger.info(buf);
  }

  std::string staSsid =
      configManager_ != nullptr
          ? std::string(configManager_->readString("wifiSsid", ""))
          : std::string();
  std::string staPass =
      configManager_ != nullptr
          ? std::string(configManager_->readString("wifiPassword", ""))
          : std::string();
  std::string staBssid =
      configManager_ != nullptr
          ? std::string(configManager_->readString("wifiBssid", ""))
          : std::string("");
  const bool wifi_full_scan =
      configManager_ != nullptr
          ? configManager_->readBool("wifiFullScan", true)
          : true;
  staConfigured_ = !staSsid.empty();

  if (!staConfigured_) {
    logger.warn("STA credentials missing, AP-only mode");
    return true;
  }

  configureStaticIpIfEnabled();

  wifi_config_t staConfig{};
  std::strncpy(reinterpret_cast<char*>(staConfig.sta.ssid), staSsid.c_str(),
               sizeof(staConfig.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(staConfig.sta.password), staPass.c_str(),
               sizeof(staConfig.sta.password) - 1);
  staConfig.sta.scan_method =
      wifi_full_scan ? WIFI_ALL_CHANNEL_SCAN : WIFI_FAST_SCAN;
  staConfig.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  staConfig.sta.threshold.authmode =
      staPass.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;

  // Parse and set BSSID if provided (format: xx:xx:xx:xx:xx:xx)
  if (!staBssid.empty()) {
    uint8_t bssid[6]{};
    if (sscanf(staBssid.c_str(), "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4],
               &bssid[5]) == 6) {
      // Use memcpy for fixed-size array copy
      std::memcpy(staConfig.sta.bssid, bssid, 6);

      staConfig.sta.bssid_set = true;
      logger.info("Using specific BSSID: " + staBssid);
    } else {
      logger.warn("Invalid BSSID format, ignoring: " + staBssid);
    }
  }

  if (esp_wifi_set_config(WIFI_IF_STA, &staConfig) != ESP_OK) {
    logger.error("STA config apply failed");
    return false;
  }
  // Keep modem sleep disabled for both bridge and INTERNAL profiles, as in
  // upstream: predictable network latency matters for both operating modes.
  if (esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
    logger.warn("Failed to configure WiFi power saving");
  }
  // Start configured installations in station-only mode. Keeping the AP radio
  // active during the initial all-channel scan can constrain the scan to the
  // temporary AP channel on some ESP32-C3/IDF combinations. The disconnect
  // handler restores AP+STA after repeated failures as a recovery path.
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
    logger.warn("Failed to switch WiFi to station mode");
  }
  char buf[64];
  snprintf(buf, sizeof(buf), "Connecting STA to SSID: %s", staSsid.c_str());
  logger.info(buf);
  setStatusLedMode(StatusLedMode::SlowBlink);
  const esp_err_t connectResult = esp_wifi_connect();
  if (connectResult != ESP_OK) {
    logger.warn(std::string("Initial STA connect failed: ") +
                esp_err_to_name(connectResult));
    scheduleReconnect(1000000);
  }
  return true;
}

uint32_t WifiNetworkManager::getLastConnect() { return lastConnect_; }

int WifiNetworkManager::getReconnectCount() { return reconnectCount_; }

bool WifiNetworkManager::isStaConnected() { return staConnected_; }

bool WifiNetworkManager::isRecoveryAccessPointReady() {
  const wifi_mode_t mode = getMode();
  return recoveryApConfigured &&
         (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
}

bool WifiNetworkManager::ensureRecoveryAccessPoint() {
  if (!recoveryApConfigured) {
    logger.error("Recovery AP configuration is unavailable");
    return false;
  }
  if (isRecoveryAccessPointReady()) return true;
  const esp_err_t result = esp_wifi_set_mode(WIFI_MODE_APSTA);
  if (result != ESP_OK) {
    logger.error(std::string("Failed to enable recovery AP: ") +
                 esp_err_to_name(result));
    return false;
  }
  logger.warn("Recovery AP enabled");
  return true;
}

std::string_view WifiNetworkManager::getIpAddress() {
  if (ipAddress_.addr != 0) return ipToString(ipAddress_);

  esp_netif_ip_info_t info{};
  if (getStaIpInfo(&info)) {
    ipAddress_ = info.ip;
    gateway_ = info.gw;
    netmask_ = info.netmask;
    return ipToString(ipAddress_);
  }
  return {};
}

void WifiNetworkManager::setStaIpAssignedCallback(
    void (*callback)(const std::string& ipAddress)) {
  staIpAssignedCallback_ = callback;

  if (staIpAssignedCallback_ == nullptr) return;

  std::string ipAddress(getIpAddress());
  if (!ipAddress.empty()) {
    staIpAssignedCallback_(ipAddress);
  }
}

bool WifiNetworkManager::isStaticIpEnabled() {
  return configManager_ != nullptr &&
         configManager_->readBool("staticIPEnabled");
}

wifi_mode_t WifiNetworkManager::getMode() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK) return WIFI_MODE_NULL;
  return mode;
}

std::string WifiNetworkManager::getConfiguredIpAddress() {
  return configManager_ != nullptr
             ? std::string(configManager_->readString("ipAddress"))
             : "";
}

std::string WifiNetworkManager::getConfiguredGateway() {
  return configManager_ != nullptr
             ? std::string(configManager_->readString("gateway"))
             : "";
}

std::string WifiNetworkManager::getConfiguredNetmask() {
  return configManager_ != nullptr
             ? std::string(configManager_->readString("netmask"))
             : "";
}

std::string WifiNetworkManager::getConfiguredDns1() {
  return configManager_ != nullptr
             ? std::string(configManager_->readString("dns1"))
             : "";
}

std::string WifiNetworkManager::getConfiguredDns2() {
  return configManager_ != nullptr
             ? std::string(configManager_->readString("dns2"))
             : "";
}

bool WifiNetworkManager::getStaIpInfo(esp_netif_ip_info_t* outInfo) {
  if (outInfo == nullptr || staNetif_ == nullptr) return false;
  esp_netif_ip_info_t info{};
  if (esp_netif_get_ip_info(staNetif_, &info) != ESP_OK) return false;
  *outInfo = info;
  return true;
}

bool WifiNetworkManager::getDnsIp(uint8_t index, esp_ip4_addr_t* outIp) {
  if (outIp == nullptr || staNetif_ == nullptr) return false;
  esp_netif_dns_info_t info{};
  const esp_netif_dns_type_t type =
      index == 0 ? ESP_NETIF_DNS_MAIN : ESP_NETIF_DNS_BACKUP;
  if (esp_netif_get_dns_info(staNetif_, type, &info) != ESP_OK) return false;
  if (info.ip.type != ESP_IPADDR_TYPE_V4) return false;
  *outIp = info.ip.u_addr.ip4;
  return true;
}

std::string_view WifiNetworkManager::ipToString(const esp_ip4_addr_t& ip) {
  thread_local static char buffer[16]{};
  if (ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t*>(&ip), buffer,
                     sizeof(buffer)) == nullptr) {
    return {};
  }
  return buffer;
}

int32_t WifiNetworkManager::RSSI() {
  wifi_ap_record_t record{};
  if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) return 0;
  return record.rssi;
}

std::string_view WifiNetworkManager::SSID() {
  thread_local static char buffer[33]{};
  wifi_ap_record_t record{};
  if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) return {};
  size_t len = strnlen(reinterpret_cast<char*>(record.ssid), 32);
  std::memcpy(buffer, record.ssid, len);
  buffer[len] = '\0';
  return buffer;
}

std::string_view WifiNetworkManager::BSSIDstr() {
  thread_local static char buffer[18]{};
  wifi_ap_record_t record{};
  if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) return {};
  std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
                record.bssid[0], record.bssid[1], record.bssid[2],
                record.bssid[3], record.bssid[4], record.bssid[5]);
  return buffer;
}

int32_t WifiNetworkManager::channel() {
  wifi_ap_record_t record{};
  if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) return 0;
  return record.primary;
}

const char* WifiNetworkManager::getHostname() {
  if (staNetif_ == nullptr) return "";
  const char* hostname = "";
  if (esp_netif_get_hostname(staNetif_, &hostname) != ESP_OK ||
      hostname == nullptr) {
    return "";
  }
  return hostname;
}

std::string_view WifiNetworkManager::macAddress() {
  thread_local static char buffer[18]{};
  uint8_t mac[6]{};
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) return {};
  std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0],
                mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buffer;
}

void WifiNetworkManager::setStatusLedPin(int pin) { statusLedPin_ = pin; }

void WifiNetworkManager::handle_event(
    void* arg, esp_event_base_t event_base, int32_t event_id,
    void* event_data) {  // cppcheck-suppress constParameterCallback
  (void)arg;

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    consecutiveDisconnects = 0;
    if (reconnectTimer != nullptr) esp_timer_stop(reconnectTimer);
    if (event_data != nullptr) {
      const auto* gotIpEvent =
          static_cast<const ip_event_got_ip_t*>(event_data);
      ipAddress_ = gotIpEvent->ip_info.ip;
      gateway_ = gotIpEvent->ip_info.gw;
      netmask_ = gotIpEvent->ip_info.netmask;
    }

    if (staIpAssignedCallback_ != nullptr) {
      std::string ipAddress(ipToString(ipAddress_));
      if (!ipAddress.empty()) {
        staIpAssignedCallback_(ipAddress);
      }
    }

    staConnected_ = true;
    setStatusLedMode(StatusLedMode::SolidOn);
    lastConnect_ = (uint32_t)(esp_timer_get_time() / 1000ULL);
    ++reconnectCount_;

  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    staConnected_ = false;
    setStatusLedMode(StatusLedMode::SlowBlink);
    logger.warn("STA disconnected, reconnecting");

    if (consecutiveDisconnects < std::numeric_limits<uint8_t>::max()) {
      ++consecutiveDisconnects;
    }
    if (consecutiveDisconnects >= 3 && getMode() == WIFI_MODE_STA) {
      ensureRecoveryAccessPoint();
    }

    if (staConfigured_) {
      const uint64_t delayUs =
          consecutiveDisconnects < 3
              ? 1000000ULL
              : std::min<uint64_t>(30000000ULL,
                                   5000000ULL * (consecutiveDisconnects - 2));
      scheduleReconnect(delayUs);
    }
  }
}

TaskHandle_t WifiNetworkManager::getStatusLedTaskHandle() {
  return statusLedTaskHandle_;
}

const char* WifiNetworkManager::powerSaveMode() {
  wifi_ps_type_t mode = WIFI_PS_NONE;
  if (esp_wifi_get_ps(&mode) != ESP_OK) return "unavailable";
  switch (mode) {
    case WIFI_PS_NONE:
      return "disabled";
    case WIFI_PS_MIN_MODEM:
      return "minimum_modem";
    case WIFI_PS_MAX_MODEM:
      return "maximum_modem";
    default:
      return "unknown";
  }
}

const char* WifiNetworkManager::scanMethod() {
  wifi_config_t config{};
  if (esp_wifi_get_config(WIFI_IF_STA, &config) != ESP_OK) {
    return "unavailable";
  }
  return config.sta.scan_method == WIFI_ALL_CHANNEL_SCAN ? "all_channels"
                                                          : "fast";
}

void WifiNetworkManager::statusLedTaskEntry(void* arg) {
  (void)arg;
  statusLedTaskLoop();
}

void WifiNetworkManager::statusLedTaskLoop() {
  bool ledOn = false;
  while (true) {
    if (statusLedPin_ < 0) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (statusLedMode_ == StatusLedMode::SolidOn) {
      gpio_set_level(static_cast<gpio_num_t>(statusLedPin_), 1);
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    ledOn = !ledOn;
    gpio_set_level(static_cast<gpio_num_t>(statusLedPin_), ledOn ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(700));
  }
}

void WifiNetworkManager::initStatusLed() {
  if (statusLedPin_ < 0) {
    return;
  }

  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << statusLedPin_;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
  gpio_set_level(static_cast<gpio_num_t>(statusLedPin_), 0);
  if (statusLedTaskHandle_ == nullptr) {
    xTaskCreate(statusLedTaskEntry, "status_led",
                app::limits::Task::status_led_stack, nullptr,
                app::limits::Task::status_led_priority, &statusLedTaskHandle_);
  }
}

void WifiNetworkManager::setStatusLedMode(StatusLedMode mode) {
  statusLedMode_ = mode;
}

void WifiNetworkManager::configureStaticIpIfEnabled() {
  if (!isStaticIpEnabled()) {
    logger.info("Static IP disabled, using DHCP");
    return;
  }

  const std::string gatewayValue = getConfiguredGateway();
  const std::string dns1Value = getConfiguredDns1();
  const std::string dns2Value = getConfiguredDns2();

  const bool valid =
      esp_netif_str_to_ip4(getConfiguredIpAddress().c_str(), &ipAddress_) &&
      esp_netif_str_to_ip4(getConfiguredNetmask().c_str(), &netmask_);
  if (valid) {
    if (!gatewayValue.empty() &&
        !esp_netif_str_to_ip4(gatewayValue.c_str(), &gateway_)) {
      logger.warn("Invalid gateway configured, using 0.0.0.0");
      // Set to 0.0.0.0 explicitly
      gateway_.addr = ESP_IP4TOADDR(0, 0, 0, 0);

    } else if (gatewayValue.empty()) {
      gateway_.addr = 0;
    }

    esp_netif_dhcpc_stop(staNetif_);

    esp_netif_ip_info_t info{};
    info.ip = ipAddress_;
    info.gw = gateway_;
    info.netmask = netmask_;
    if (esp_netif_set_ip_info(staNetif_, &info) != ESP_OK) {
      logger.error("Failed to set static IP info");
      return;
    }
    bool dns1IsValid = true;
    if (!dns1Value.empty())
      dns1IsValid = esp_netif_str_to_ip4(dns1Value.c_str(), &dns1_);
    if (!dns1IsValid)
      logger.warn("Invalid DNS1 configured, ignoring");
    else {
      esp_netif_dns_info_t dns{};
      dns.ip.u_addr.ip4 = dns1_;
      dns.ip.type = ESP_IPADDR_TYPE_V4;
      esp_netif_set_dns_info(staNetif_, ESP_NETIF_DNS_MAIN, &dns);
    }

    bool dns2IsValid = true;
    if (!dns2Value.empty())
      dns2IsValid = esp_netif_str_to_ip4(dns2Value.c_str(), &dns2_);
    if (!dns2IsValid)
      logger.warn("Invalid DNS2 configured, ignoring");
    else {
      esp_netif_dns_info_t dns{};
      dns.ip.u_addr.ip4 = dns2_;
      dns.ip.type = ESP_IPADDR_TYPE_V4;
      esp_netif_set_dns_info(staNetif_, ESP_NETIF_DNS_BACKUP, &dns);
    }

    char buf[128];
    snprintf(buf, sizeof(buf),
             "Static IP configured: %s, Gateway: %s, DNS1: %s%s",
             ipToString(ipAddress_).data(), ipToString(gateway_).data(),
             ipToString(dns1_).data(),
             (dns2Value.empty()
                  ? ""
                  : std::string(", DNS2: ").append(ipToString(dns2_)).c_str()));
    logger.info(buf);
  } else {  // Use snprintf for warning message
    logger.warn("Invalid static IP/netmask config, falling back to DHCP");
  }
}

// Console-only diagnostics (see header): full WiFi slice with password.
// Worst case ~261 chars (bounded by FixedString sizes), fits max_msg_length.
void WifiNetworkManager::logWifiSlice() {
  const std::string ssid =
      configManager_ != nullptr
          ? std::string(configManager_->readString("wifiSsid", ""))
          : std::string("");
  const std::string pass =
      configManager_ != nullptr
          ? std::string(configManager_->readString("wifiPassword", ""))
          : std::string("");
  const std::string bssid =
      configManager_ != nullptr
          ? std::string(configManager_->readString("wifiBssid", ""))
          : std::string("");
  char buf[320];
  const int n = snprintf(
      buf, sizeof(buf),
      "wifi: ssid='%s' pass='%s' bssid='%s' static=%s ip=%s gw=%s mask=%s "
      "dns1=%s dns2=%s",
      ssid.c_str(), pass.c_str(), bssid.c_str(),
      isStaticIpEnabled() ? "true" : "false", getConfiguredIpAddress().c_str(),
      getConfiguredGateway().c_str(), getConfiguredNetmask().c_str(),
      getConfiguredDns1().c_str(), getConfiguredDns2().c_str());
  if (n > 0) logger.warn(buf);
}

void appendWifiStatus(ebus::detail::JsonWriter& writer) {
  writer.writeField("last_connect", WifiNetworkManager::getLastConnect());
  writer.writeField("reconnect_count", WifiNetworkManager::getReconnectCount());
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
    writer.writeField(
        "dns2", hasDnsBackup ? WifiNetworkManager::ipToString(dnsBackup) : "");
  }
  writer.writeField("ssid", WifiNetworkManager::SSID());
  writer.writeField("bssid", WifiNetworkManager::BSSIDstr());
  writer.writeField("channel", WifiNetworkManager::channel());
  writer.writeField("power_save", WifiNetworkManager::powerSaveMode());
  writer.writeField("scan_method", WifiNetworkManager::scanMethod());
  writer.writeField("hostname", WifiNetworkManager::getHostname());
  writer.writeField("mac_address", WifiNetworkManager::macAddress());
}
