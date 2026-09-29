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
#include <string>

#include "app/app_limits.hpp"
#include "config/config_manager.hpp"
#include "network/detail/wifi.hpp"
#include "system/logger.hpp"

namespace {
constexpr int max_consecutive_failures = 5;
}  // namespace

ConfigManager* WifiNetworkManager::config_manager_ = nullptr;
esp_ip4_addr_t WifiNetworkManager::ip_address_{};
esp_ip4_addr_t WifiNetworkManager::gateway_{};
esp_ip4_addr_t WifiNetworkManager::netmask_{};
esp_ip4_addr_t WifiNetworkManager::dns1_{};
esp_ip4_addr_t WifiNetworkManager::dns2_{};
uint32_t WifiNetworkManager::last_connect_ = 0;
int WifiNetworkManager::reconnect_count_ = 0;
int WifiNetworkManager::consecutive_failures_ = 0;
bool WifiNetworkManager::sta_connected_ = false;
bool WifiNetworkManager::sta_configured_ = false;
TaskHandle_t WifiNetworkManager::status_led_task_handle_ = nullptr;
volatile WifiNetworkManager::StatusLedMode
    WifiNetworkManager::status_led_mode_ =
        WifiNetworkManager::StatusLedMode::slow_blink;
int WifiNetworkManager::status_led_pin_ = -1;
void (*WifiNetworkManager::sta_ip_assigned_callback_)(
    const std::string& ip_address) = nullptr;
esp_netif_t* WifiNetworkManager::sta_netif_ = nullptr;
esp_netif_t* WifiNetworkManager::ap_netif_ = nullptr;

void WifiNetworkManager::begin(ConfigManager* config_manager) {
  static constexpr const char* default_hostname = "esp-eBus";
  static constexpr const char* default_ap_ssid = "esp-eBus";
  static constexpr const char* default_ap_password = "ebusebus";
  static bool started = false;
  if (started) return;
  started = true;

  config_manager_ = config_manager;
  initStatusLed();
  setStatusLedMode(StatusLedMode::slow_blink);

  std::string ap_password = config_manager_ != nullptr
                                ? std::string(config_manager_->readString(
                                      "apModePassword", default_ap_password))
                                : std::string(default_ap_password);
  if (ap_password.empty()) ap_password = default_ap_password;
  const std::string configured_thing_name =
      config_manager_ != nullptr ? std::string(config_manager_->readString(
                                       "thingName", default_hostname))
                                 : std::string(default_hostname);
  const std::string hostname = network::detail::wifi::buildHostname(
      configured_thing_name, default_hostname);

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    logger.error("esp_netif_init failed");
    return;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    logger.error("esp_event_loop_create_default failed");
    return;
  }

  if (sta_netif_ == nullptr) {
    sta_netif_ = esp_netif_create_default_wifi_sta();
  }
  if (ap_netif_ == nullptr) {
    ap_netif_ = esp_netif_create_default_wifi_ap();
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) != ESP_OK) {
    logger.error("esp_wifi_init failed");
    return;
  }

  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      &WifiNetworkManager::handleEvent, nullptr,
                                      nullptr);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                      &WifiNetworkManager::handleEvent, nullptr,
                                      nullptr);

  esp_wifi_set_storage(false ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM);

  esp_netif_set_hostname(sta_netif_, hostname.c_str());

  if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
    logger.error("Failed to set WiFi mode");
    return;
  }

  if (esp_wifi_start() != ESP_OK) {
    logger.error("Failed to start WiFi");
    return;
  }

  // Initialize mDNS
  esp_err_t mdns_err = mdns_init();
  if (mdns_err != ESP_OK) {
    char buf[32];
    snprintf(buf, sizeof(buf), "mdns_init failed: %d", mdns_err);
    logger.warn(buf);
  } else {
    mdns_hostname_set(hostname.c_str());
    mdns_instance_name_set(hostname.c_str());
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    logger.info("mDNS started: " + hostname + ".local");
  }

  wifi_config_t ap_config{};
  std::strncpy(reinterpret_cast<char*>(ap_config.ap.ssid), default_ap_ssid,
               sizeof(ap_config.ap.ssid) - 1);
  ap_config.ap.ssid_len = std::strlen(default_ap_ssid);
  std::strncpy(reinterpret_cast<char*>(ap_config.ap.password),
               ap_password.c_str(), sizeof(ap_config.ap.password) - 1);
  ap_config.ap.max_connection = 4;
  ap_config.ap.channel = rand() % 12 + 1;
  ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  if (ap_password.size() < 8) ap_config.ap.authmode = WIFI_AUTH_OPEN;
  if (esp_wifi_set_config(WIFI_IF_AP, &ap_config) != ESP_OK) {
    logger.error("AP config apply failed");
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "AP ready: %s (%s)", default_ap_ssid,
             (ap_config.ap.authmode == WIFI_AUTH_OPEN ? "open" : "wpa2"));
    logger.info(buf);
  }

  std::string sta_ssid =
      config_manager_ != nullptr
          ? std::string(config_manager_->readString("wifiSsid", "ebus-test"))
          : std::string("ebus-test");
  std::string sta_pass =
      config_manager_ != nullptr
          ? std::string(config_manager_->readString("wifiPassword", "lectronz"))
          : std::string("lectronz");
  std::string sta_bssid =
      config_manager_ != nullptr
          ? std::string(config_manager_->readString("wifiBssid", ""))
          : std::string("");
  sta_configured_ = !sta_ssid.empty();

  if (!sta_configured_) {
    logger.warn("STA credentials missing, AP-only mode");
    return;
  }

  configureStaticIpIfEnabled();

  wifi_config_t sta_config{};
  std::strncpy(reinterpret_cast<char*>(sta_config.sta.ssid), sta_ssid.c_str(),
               sizeof(sta_config.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(sta_config.sta.password),
               sta_pass.c_str(), sizeof(sta_config.sta.password) - 1);

  // Parse and set BSSID if provided (format: xx:xx:xx:xx:xx:xx)
  if (!sta_bssid.empty()) {
    uint8_t bssid[6]{};
    if (sscanf(sta_bssid.c_str(), "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4],
               &bssid[5]) == 6) {
      // Use memcpy for fixed-size array copy
      std::memcpy(sta_config.sta.bssid, bssid, 6);

      sta_config.sta.bssid_set = true;
      logger.info("Using specific BSSID: " + sta_bssid);
    } else {
      logger.warn("Invalid BSSID format, ignoring: " + sta_bssid);
    }
  }

  if (esp_wifi_set_config(WIFI_IF_STA, &sta_config) != ESP_OK) {
    logger.error("STA config apply failed");
    return;
  }
  char buf[64];
  snprintf(buf, sizeof(buf), "Connecting STA to SSID: %s", sta_ssid.c_str());
  logger.info(buf);
  setStatusLedMode(StatusLedMode::slow_blink);
  // No modem sleep on a mains-powered bus adapter: DTIM-gated RX adds
  // up to hundreds of ms link jitter and wake bursts preempt everything
  // below WiFi priority (including the bus thread) at the worst moment.
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_connect();
}

uint32_t WifiNetworkManager::getLastConnect() { return last_connect_; }

int WifiNetworkManager::getReconnectCount() { return reconnect_count_; }

bool WifiNetworkManager::isStaConnected() { return sta_connected_; }

std::string_view WifiNetworkManager::getIpAddress() {
  if (ip_address_.addr != 0) return ipToString(ip_address_);

  esp_netif_ip_info_t info{};
  if (getStaIpInfo(&info)) {
    ip_address_ = info.ip;
    gateway_ = info.gw;
    netmask_ = info.netmask;
    return ipToString(ip_address_);
  }
  return {};
}

void WifiNetworkManager::setStaIpAssignedCallback(
    void (*callback)(const std::string& ip_address)) {
  sta_ip_assigned_callback_ = callback;

  if (sta_ip_assigned_callback_ == nullptr) return;

  std::string ip_address(getIpAddress());
  if (!ip_address.empty()) {
    sta_ip_assigned_callback_(ip_address);
  }
}

bool WifiNetworkManager::isStaticIpEnabled() {
  return config_manager_ != nullptr &&
         config_manager_->readBool("staticIPEnabled");
}

wifi_mode_t WifiNetworkManager::getMode() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK) return WIFI_MODE_NULL;
  return mode;
}

std::string WifiNetworkManager::getConfiguredIpAddress() {
  return config_manager_ != nullptr
             ? std::string(config_manager_->readString("ipAddress"))
             : "";
}

std::string WifiNetworkManager::getConfiguredGateway() {
  return config_manager_ != nullptr
             ? std::string(config_manager_->readString("gateway"))
             : "";
}

std::string WifiNetworkManager::getConfiguredNetmask() {
  return config_manager_ != nullptr
             ? std::string(config_manager_->readString("netmask"))
             : "";
}

std::string WifiNetworkManager::getConfiguredDns1() {
  return config_manager_ != nullptr
             ? std::string(config_manager_->readString("dns1"))
             : "";
}

std::string WifiNetworkManager::getConfiguredDns2() {
  return config_manager_ != nullptr
             ? std::string(config_manager_->readString("dns2"))
             : "";
}

bool WifiNetworkManager::getStaIpInfo(esp_netif_ip_info_t* out_info) {
  if (out_info == nullptr || sta_netif_ == nullptr) return false;
  esp_netif_ip_info_t info{};
  if (esp_netif_get_ip_info(sta_netif_, &info) != ESP_OK) return false;
  *out_info = info;
  return true;
}

bool WifiNetworkManager::getDnsIp(uint8_t index, esp_ip4_addr_t* out_ip) {
  if (out_ip == nullptr || sta_netif_ == nullptr) return false;
  esp_netif_dns_info_t info{};
  const esp_netif_dns_type_t type =
      index == 0 ? ESP_NETIF_DNS_MAIN : ESP_NETIF_DNS_BACKUP;
  if (esp_netif_get_dns_info(sta_netif_, type, &info) != ESP_OK) return false;
  if (info.ip.type != ESP_IPADDR_TYPE_V4) return false;
  *out_ip = info.ip.u_addr.ip4;
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

int32_t WifiNetworkManager::rssi() {
  wifi_ap_record_t record{};
  if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) return 0;
  return record.rssi;
}

std::string_view WifiNetworkManager::ssid() {
  thread_local static char buffer[33]{};
  wifi_ap_record_t record{};
  if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) return {};
  size_t len = strnlen(reinterpret_cast<char*>(record.ssid), 32);
  std::memcpy(buffer, record.ssid, len);
  buffer[len] = '\0';
  return buffer;
}

std::string_view WifiNetworkManager::bssidStr() {
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
  if (sta_netif_ == nullptr) return "";
  const char* hostname = "";
  if (esp_netif_get_hostname(sta_netif_, &hostname) != ESP_OK ||
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

void WifiNetworkManager::setStatusLedPin(int pin) { status_led_pin_ = pin; }

void WifiNetworkManager::handleEvent(
    void* arg, esp_event_base_t event_base, int32_t event_id,
    void* event_data) {  // cppcheck-suppress constParameterCallback
  (void)arg;
  (void)event_base;

  if (event_id == IP_EVENT_STA_GOT_IP) {
    if (event_data != nullptr) {
      const auto* got_ip_event =
          static_cast<const ip_event_got_ip_t*>(event_data);
      ip_address_ = got_ip_event->ip_info.ip;
      gateway_ = got_ip_event->ip_info.gw;
      netmask_ = got_ip_event->ip_info.netmask;
    }

    if (sta_ip_assigned_callback_ != nullptr) {
      std::string ip_address(ipToString(ip_address_));
      if (!ip_address.empty()) {
        sta_ip_assigned_callback_(ip_address);
      }
    }

    sta_connected_ = true;
    consecutive_failures_ = 0;
    setStatusLedMode(StatusLedMode::solid_on);
    last_connect_ = (uint32_t)(esp_timer_get_time() / 1000ULL);
    ++reconnect_count_;

    if (getMode() != WIFI_MODE_STA) {
      if (esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK) {
        logger.info("Switched WiFi mode to STA only");
      } else {
        logger.warn("Failed to switch WiFi mode to STA only");
      }
    }
  } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    sta_connected_ = false;
    setStatusLedMode(StatusLedMode::slow_blink);
    logger.warn("STA disconnected, reconnecting");

    if (++consecutive_failures_ == max_consecutive_failures) logWifiSlice();

    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
      logger.error("Failed to set WiFi mode");
      return;
    }

    if (sta_configured_) esp_wifi_connect();
  }
}

TaskHandle_t WifiNetworkManager::getStatusLedTaskHandle() {
  return status_led_task_handle_;
}

void WifiNetworkManager::statusLedTaskEntry(void* arg) {
  (void)arg;
  statusLedTaskLoop();
}

void WifiNetworkManager::statusLedTaskLoop() {
  bool led_on = false;
  while (true) {
    if (status_led_pin_ < 0) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (status_led_mode_ == StatusLedMode::solid_on) {
      gpio_set_level(static_cast<gpio_num_t>(status_led_pin_), 1);
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    led_on = !led_on;
    gpio_set_level(static_cast<gpio_num_t>(status_led_pin_), led_on ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(700));
  }
}

void WifiNetworkManager::initStatusLed() {
  if (status_led_pin_ < 0) {
    return;
  }

  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << status_led_pin_;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
  gpio_set_level(static_cast<gpio_num_t>(status_led_pin_), 0);
  if (status_led_task_handle_ == nullptr) {
    xTaskCreate(statusLedTaskEntry, "status_led",
                app::limits::Task::status_led_stack, nullptr,
                app::limits::Task::status_led_priority,
                &status_led_task_handle_);
  }
}

void WifiNetworkManager::setStatusLedMode(StatusLedMode mode) {
  status_led_mode_ = mode;
}

void WifiNetworkManager::configureStaticIpIfEnabled() {
  if (!isStaticIpEnabled()) {
    logger.info("Static IP disabled, using DHCP");
    return;
  }

  const std::string gateway_value = getConfiguredGateway();
  const std::string dns1_value = getConfiguredDns1();
  const std::string dns2_value = getConfiguredDns2();

  const bool valid =
      esp_netif_str_to_ip4(getConfiguredIpAddress().c_str(), &ip_address_) &&
      esp_netif_str_to_ip4(getConfiguredNetmask().c_str(), &netmask_);
  if (valid) {
    if (!gateway_value.empty() &&
        !esp_netif_str_to_ip4(gateway_value.c_str(), &gateway_)) {
      logger.warn("Invalid gateway configured, using 0.0.0.0");
      // Set to 0.0.0.0 explicitly
      gateway_.addr = ESP_IP4TOADDR(0, 0, 0, 0);

    } else if (gateway_value.empty()) {
      gateway_.addr = 0;
    }

    esp_netif_dhcpc_stop(sta_netif_);

    esp_netif_ip_info_t info{};
    info.ip = ip_address_;
    info.gw = gateway_;
    info.netmask = netmask_;
    if (esp_netif_set_ip_info(sta_netif_, &info) != ESP_OK) {
      logger.error("Failed to set static IP info");
      return;
    }
    bool dns1_is_valid = true;
    if (!dns1_value.empty())
      dns1_is_valid = esp_netif_str_to_ip4(dns1_value.c_str(), &dns1_);
    if (!dns1_is_valid)
      logger.warn("Invalid DNS1 configured, ignoring");
    else {
      esp_netif_dns_info_t dns{};
      dns.ip.u_addr.ip4 = dns1_;
      dns.ip.type = ESP_IPADDR_TYPE_V4;
      esp_netif_set_dns_info(sta_netif_, ESP_NETIF_DNS_MAIN, &dns);
    }

    bool dns2_is_valid = true;
    if (!dns2_value.empty())
      dns2_is_valid = esp_netif_str_to_ip4(dns2_value.c_str(), &dns2_);
    if (!dns2_is_valid)
      logger.warn("Invalid DNS2 configured, ignoring");
    else {
      esp_netif_dns_info_t dns{};
      dns.ip.u_addr.ip4 = dns2_;
      dns.ip.type = ESP_IPADDR_TYPE_V4;
      esp_netif_set_dns_info(sta_netif_, ESP_NETIF_DNS_BACKUP, &dns);
    }

    char buf[128];
    snprintf(buf, sizeof(buf),
             "Static IP configured: %s, Gateway: %s, DNS1: %s%s",
             ipToString(ip_address_).data(), ipToString(gateway_).data(),
             ipToString(dns1_).data(),
             (dns2_value.empty()
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
      config_manager_ != nullptr
          ? std::string(config_manager_->readString("wifiSsid", ""))
          : std::string("");
  const std::string pass =
      config_manager_ != nullptr
          ? std::string(config_manager_->readString("wifiPassword", ""))
          : std::string("");
  const std::string bssid =
      config_manager_ != nullptr
          ? std::string(config_manager_->readString("wifiBssid", ""))
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
  writer.writeField("rssi", WifiNetworkManager::rssi());
  if (WifiNetworkManager::isStaticIpEnabled()) {
    writer.writeField("static_ip", true);
    writer.writeField("ip_address",
                      WifiNetworkManager::getConfiguredIpAddress());
    writer.writeField("gateway", WifiNetworkManager::getConfiguredGateway());
    writer.writeField("netmask", WifiNetworkManager::getConfiguredNetmask());
    writer.writeField("dns1", WifiNetworkManager::getConfiguredDns1());
    writer.writeField("dns2", WifiNetworkManager::getConfiguredDns2());
  } else {
    esp_netif_ip_info_t sta_ip_info{};
    const bool has_sta_ip = WifiNetworkManager::getStaIpInfo(&sta_ip_info);
    esp_ip4_addr_t dns_main{}, dns_backup{};
    const bool has_dns_main = WifiNetworkManager::getDnsIp(0, &dns_main);
    const bool has_dns_backup = WifiNetworkManager::getDnsIp(1, &dns_backup);
    writer.writeField("static_ip", false);
    writer.writeField(
        "ip_address",
        has_sta_ip ? WifiNetworkManager::ipToString(sta_ip_info.ip) : "");
    writer.writeField(
        "gateway",
        has_sta_ip ? WifiNetworkManager::ipToString(sta_ip_info.gw) : "");
    writer.writeField(
        "netmask",
        has_sta_ip ? WifiNetworkManager::ipToString(sta_ip_info.netmask) : "");
    writer.writeField(
        "dns1", has_dns_main ? WifiNetworkManager::ipToString(dns_main) : "");
    writer.writeField("dns2", has_dns_backup
                                  ? WifiNetworkManager::ipToString(dns_backup)
                                  : "");
  }
  writer.writeField("ssid", WifiNetworkManager::ssid());
  writer.writeField("bssid", WifiNetworkManager::bssidStr());
  writer.writeField("channel", WifiNetworkManager::channel());
  writer.writeField("hostname", WifiNetworkManager::getHostname());
  writer.writeField("mac_address", WifiNetworkManager::macAddress());
}
