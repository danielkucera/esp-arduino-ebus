#include "WifiNetworkManager.hpp"

#include <cctype>
#include <cstring>
#include <cstdlib>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/ip4_addr.h>
#include <nvs_flash.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "ConfigManager.hpp"
#include "Logger.hpp"

ConfigManager* WifiNetworkManager::configManager_ = nullptr;
esp_ip4_addr_t WifiNetworkManager::ipAddress_{};
esp_ip4_addr_t WifiNetworkManager::gateway_{};
esp_ip4_addr_t WifiNetworkManager::netmask_{};
esp_ip4_addr_t WifiNetworkManager::dns1_{};
esp_ip4_addr_t WifiNetworkManager::dns2_{};
uint32_t WifiNetworkManager::lastConnect_ = 0;
int WifiNetworkManager::reconnectCount_ = 0;
bool WifiNetworkManager::staConnected_ = false;
bool WifiNetworkManager::staConfigured_ = false;
TaskHandle_t WifiNetworkManager::statusLedTaskHandle_ = nullptr;
volatile WifiNetworkManager::StatusLedMode WifiNetworkManager::statusLedMode_ =
  WifiNetworkManager::StatusLedMode::SlowBlink;
esp_netif_t* WifiNetworkManager::staNetif_ = nullptr;
esp_netif_t* WifiNetworkManager::apNetif_ = nullptr;

namespace {

std::string trimCopy(const std::string& value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

std::string buildHostname(const std::string& source, const char* fallback) {
  std::string hostname = trimCopy(source);
  if (hostname.empty()) hostname = fallback;

  std::string sanitized;
  sanitized.reserve(hostname.size());
  for (size_t i = 0; i < hostname.size(); ++i) {
    const char c = hostname[i];
    if (std::isalnum(static_cast<unsigned char>(c))) {
      sanitized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (c == '-' || c == '_' || c == ' ') {
      sanitized += '-';
    }
  }

  while (!sanitized.empty() && sanitized.front() == '-') {
    sanitized.erase(sanitized.begin());
  }
  while (!sanitized.empty() && sanitized.back() == '-') sanitized.pop_back();

  if (sanitized.empty()) sanitized = "esp-ebus";
  if (sanitized.size() > 63) sanitized.erase(63);
  return sanitized;
}

}  // namespace

void WifiNetworkManager::begin(ConfigManager* configManager) {
  static constexpr const char* kDefaultHostname = "esp-eBus";
  static constexpr const char* kDefaultApSsid = "esp-eBus";
  static constexpr const char* kDefaultApPassword = "ebusebus";

  configManager_ = configManager;
  initStatusLed();
  setStatusLedMode(StatusLedMode::SlowBlink);

  std::string apPassword = configManager_ != nullptr
                               ? configManager_->readString("apModePassword",
                                                            kDefaultApPassword)
                               : std::string(kDefaultApPassword);
  if (apPassword.empty()) apPassword = kDefaultApPassword;
  const std::string configuredThingName =
      configManager_ != nullptr
          ? configManager_->readString("thingName", kDefaultHostname)
          : std::string(kDefaultHostname);
  const std::string hostname =
      buildHostname(configuredThingName, kDefaultHostname);

  static bool nvsReady = false;
  if (!nvsReady) {
    esp_err_t nvsErr = nvs_flash_init();
    if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      nvsErr = nvs_flash_init();
    }
    if (nvsErr != ESP_OK) {
      logger.error("nvs_flash_init failed");
      return;
    }
    nvsReady = true;
  }

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

  if (staNetif_ == nullptr) {
    staNetif_ = esp_netif_create_default_wifi_sta();
  }
  if (apNetif_ == nullptr) {
    apNetif_ = esp_netif_create_default_wifi_ap();
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) != ESP_OK) {
    logger.error("esp_wifi_init failed");
    return;
  }

  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      &WifiNetworkManager::handle_event, nullptr,
                                      nullptr);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                      &WifiNetworkManager::handle_event, nullptr, nullptr);

  //initialized_ = true;

  esp_wifi_set_storage(false ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM);

  //esp_wifi_set_auto_connect(true);

  esp_netif_set_hostname(staNetif_, hostname.c_str());

  if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
    logger.error("Failed to set WiFi mode");
    return;
  }

  if (esp_wifi_start() != ESP_OK) {
    logger.error("Failed to start WiFi");
    return;
  }
  
  logger.info("mDNS disabled in ESP-IDF build");

  wifi_config_t apConfig{};
  std::strncpy(reinterpret_cast<char*>(apConfig.ap.ssid), kDefaultApSsid,
               sizeof(apConfig.ap.ssid) - 1);
  apConfig.ap.ssid_len = std::strlen(kDefaultApSsid);
  std::strncpy(reinterpret_cast<char*>(apConfig.ap.password), apPassword.c_str(),
               sizeof(apConfig.ap.password) - 1);
  apConfig.ap.max_connection = 4;
  apConfig.ap.channel = rand() % 12 + 1;
  apConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
  if (apPassword.size() < 8) apConfig.ap.authmode = WIFI_AUTH_OPEN;
  if (esp_wifi_set_config(WIFI_IF_AP, &apConfig) != ESP_OK) {
    logger.error("AP config apply failed");
  } else {
    logger.info(std::string("AP ready: ") + kDefaultApSsid + " (" +
                (apConfig.ap.authmode == WIFI_AUTH_OPEN ? "open" : "wpa2") +
                ")");
  }

  std::string staSsid =
      configManager_ != nullptr
          ? configManager_->readString("wifiSsid", "ebus-test")
          : std::string("ebus-test");
  std::string staPass =
      configManager_ != nullptr
          ? configManager_->readString("wifiPassword", "lectronz")
          : std::string("lectronz");
  staConfigured_ = !staSsid.empty();

  if (!staConfigured_) {
    logger.warn("STA credentials missing, AP-only mode");
    return;
  }

  configureStaticIpIfEnabled();

  wifi_config_t staConfig{};
  std::strncpy(reinterpret_cast<char*>(staConfig.sta.ssid), staSsid.c_str(),
               sizeof(staConfig.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(staConfig.sta.password), staPass.c_str(),
               sizeof(staConfig.sta.password) - 1);
  if (esp_wifi_set_config(WIFI_IF_STA, &staConfig) != ESP_OK) {
    logger.error("STA config apply failed");
    return;
  }
  logger.info("Connecting STA to SSID: " + staSsid);
  setStatusLedMode(StatusLedMode::SlowBlink);
  esp_wifi_connect();
}

uint32_t WifiNetworkManager::getLastConnect() { return lastConnect_; }

int WifiNetworkManager::getReconnectCount() { return reconnectCount_; }

bool WifiNetworkManager::isStaConnected() { return staConnected_; }

wifi_mode_t WifiNetworkManager::getMode() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK) return WIFI_MODE_NULL;
  return mode;
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

std::string WifiNetworkManager::ipToString(const esp_ip4_addr_t& ip) {
  char buffer[16]{};
  if (ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t*>(&ip), buffer,
                     sizeof(buffer)) == nullptr) {
    return "";
  }
  return buffer;
}

int32_t WifiNetworkManager::RSSI() {
  wifi_ap_record_t record{};
  if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) return 0;
  return record.rssi;
}

std::string WifiNetworkManager::SSID() {
  wifi_ap_record_t record{};
  if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) return "";
  return std::string(reinterpret_cast<char*>(record.ssid));
}

std::string WifiNetworkManager::BSSIDstr() {
  wifi_ap_record_t record{};
  if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) return "";
  char buffer[18]{};
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
  if (esp_netif_get_hostname(staNetif_, &hostname) != ESP_OK || hostname == nullptr) {
    return "";
  }
  return hostname;
}

std::string WifiNetworkManager::macAddress() {
  uint8_t mac[6]{};
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) return "";
  char buffer[18]{};
  std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buffer;
}

void WifiNetworkManager::statusLedTaskEntry(void* arg) {
  (void)arg;
  statusLedTaskLoop();
}

void WifiNetworkManager::statusLedTaskLoop() {
#if defined(STATUS_LED_PIN)
  bool ledOn = false;
  while (true) {
    if (statusLedMode_ == StatusLedMode::SolidOn) {
      gpio_set_level(static_cast<gpio_num_t>(STATUS_LED_PIN), 1);
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    ledOn = !ledOn;
    gpio_set_level(static_cast<gpio_num_t>(STATUS_LED_PIN), ledOn ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(700));
  }
#else
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
#endif
}

void WifiNetworkManager::initStatusLed() {
#if defined(STATUS_LED_PIN)
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << STATUS_LED_PIN;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
  gpio_set_level(static_cast<gpio_num_t>(STATUS_LED_PIN), 0);
  if (statusLedTaskHandle_ == nullptr) {
    xTaskCreate(statusLedTaskEntry, "status_led_task", 2048, nullptr, 1,
                &statusLedTaskHandle_);
  }
#endif
}

void WifiNetworkManager::setStatusLedMode(StatusLedMode mode) {
  statusLedMode_ = mode;
}

bool WifiNetworkManager::isStaticIpEnabled() {
  return configManager_ != nullptr && configManager_->readBool("staticIPEnabled");
}

std::string WifiNetworkManager::getConfiguredIpAddress() {
  return configManager_ != nullptr ? configManager_->readString("ipAddress") : "";
}

std::string WifiNetworkManager::getConfiguredGateway() {
  return configManager_ != nullptr ? configManager_->readString("gateway") : "";
}

std::string WifiNetworkManager::getConfiguredNetmask() {
  return configManager_ != nullptr ? configManager_->readString("netmask") : "";
}

std::string WifiNetworkManager::getConfiguredDns1() {
  return configManager_ != nullptr ? configManager_->readString("dns1") : "";
}

std::string WifiNetworkManager::getConfiguredDns2() {
  return configManager_ != nullptr ? configManager_->readString("dns2") : "";
}

void WifiNetworkManager::handle_event(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
  (void)arg;
  (void)event_base;
  (void)event_data;

  if (event_id == IP_EVENT_STA_GOT_IP) {
    staConnected_ = true;
    setStatusLedMode(StatusLedMode::SolidOn);
    lastConnect_ = (uint32_t)(esp_timer_get_time() / 1000ULL);
    ++reconnectCount_;
//    logger.info("STA connected, IP: " +
//                std::string(WiFi.localIP().toString().c_str()));
    if (getMode() != WIFI_MODE_STA) {
      if (esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK) {
        logger.info("Switched WiFi mode to STA only");
      } else {
        logger.warn("Failed to switch WiFi mode to STA only");
      }
    }
  } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    staConnected_ = false;
    setStatusLedMode(StatusLedMode::SlowBlink);
    logger.warn("STA disconnected, reconnecting");
    if (staConfigured_) esp_wifi_connect();
  }
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
      gateway_.addr = 0;
    } else if (gatewayValue.empty()) {
      gateway_.addr = 0;
    }

    //WiFi.config(ipAddress_, gateway_, netmask_, dns1, dns2);

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
    if (!dns1IsValid) logger.warn("Invalid DNS1 configured, ignoring");
    else {
      esp_netif_dns_info_t dns{};
      dns.ip.u_addr.ip4 = dns1_;
      dns.ip.type = ESP_IPADDR_TYPE_V4;
      esp_netif_set_dns_info(staNetif_, ESP_NETIF_DNS_MAIN, &dns);  
    }

    bool dns2IsValid = true;
    if (!dns2Value.empty())
      dns2IsValid = esp_netif_str_to_ip4(dns2Value.c_str(), &dns2_);
    if (!dns2IsValid) logger.warn("Invalid DNS2 configured, ignoring");
    else {
       esp_netif_dns_info_t dns{};
       dns.ip.u_addr.ip4 = dns2_;
       dns.ip.type = ESP_IPADDR_TYPE_V4;
       esp_netif_set_dns_info(staNetif_, ESP_NETIF_DNS_BACKUP, &dns);  
    }

    logger.info(
        std::string("Static IP configured: ") +
        ipToString(ipAddress_) + ", Gateway: " + ipToString(gateway_) +
        ", DNS1: " + ipToString(dns1_) +
        (dns2Value.empty()
             ? ""
             : ", DNS2: " + ipToString(dns2_)));
  } else {
    logger.warn("Invalid static IP/netmask config, falling back to DHCP");
  }
}
