#include "WifiNetworkManager.hpp"

#include <cctype>
#include <cstring>
#include <cstdlib>
#include <driver/gpio.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <string>

#include "ConfigManager.hpp"
#include "Logger.hpp"

WifiNetworkManager* WifiNetworkManager::instance_ = nullptr;

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
  instance_ = this;
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

  WiFi.onEvent(onWiFiEventStatic);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(hostname.c_str());
  WiFi.mode(WIFI_AP_STA);
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
  dnsServer_.start(53, "*", WiFi.softAPIP());
  if (dnsTaskHandle_ == nullptr) {
    xTaskCreate(dnsTaskEntry, "dns_task", 4096, this, 1, &dnsTaskHandle_);
    logger.info("Captive DNS task started");
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

uint32_t WifiNetworkManager::getLastConnect() const { return lastConnect_; }

int WifiNetworkManager::getReconnectCount() const { return reconnectCount_; }

bool WifiNetworkManager::isStaConnected() const { return staConnected_; }

bool WifiNetworkManager::isCaptivePortalActive() const {
  return !staConnected_ && WiFi.getMode() != WIFI_MODE_STA;
}

void WifiNetworkManager::dnsTaskEntry(void* arg) {
  WifiNetworkManager* self = static_cast<WifiNetworkManager*>(arg);
  self->dnsTaskLoop();
}

void WifiNetworkManager::dnsTaskLoop() {
  while (true) {
    if (isCaptivePortalActive()) {
      dnsServer_.processNextRequest();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void WifiNetworkManager::statusLedTaskEntry(void* arg) {
  WifiNetworkManager* self = static_cast<WifiNetworkManager*>(arg);
  self->statusLedTaskLoop();
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
    xTaskCreate(statusLedTaskEntry, "status_led_task", 2048, this, 1,
                &statusLedTaskHandle_);
  }
#endif
}

void WifiNetworkManager::setStatusLedMode(StatusLedMode mode) {
  statusLedMode_ = mode;
}

bool WifiNetworkManager::isStaticIpEnabled() const {
  return configManager_ != nullptr && configManager_->readBool("staticIPEnabled");
}

std::string WifiNetworkManager::getConfiguredIpAddress() const {
  return configManager_ != nullptr ? configManager_->readString("ipAddress") : "";
}

std::string WifiNetworkManager::getConfiguredGateway() const {
  return configManager_ != nullptr ? configManager_->readString("gateway") : "";
}

std::string WifiNetworkManager::getConfiguredNetmask() const {
  return configManager_ != nullptr ? configManager_->readString("netmask") : "";
}

std::string WifiNetworkManager::getConfiguredDns1() const {
  return configManager_ != nullptr ? configManager_->readString("dns1") : "";
}

std::string WifiNetworkManager::getConfiguredDns2() const {
  return configManager_ != nullptr ? configManager_->readString("dns2") : "";
}

void WifiNetworkManager::onWiFiEventStatic(WiFiEvent_t event,
                                           WiFiEventInfo_t info) {
  if (instance_ != nullptr) instance_->onWiFiEvent(event, info);
}

void WifiNetworkManager::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    staConnected_ = true;
    setStatusLedMode(StatusLedMode::SolidOn);
    lastConnect_ = (uint32_t)(esp_timer_get_time() / 1000ULL);
    ++reconnectCount_;
    logger.info("STA connected, IP: " +
                std::string(WiFi.localIP().toString().c_str()));
    dnsServer_.stop();
    logger.info("Captive DNS stopped");
    if (WiFi.getMode() != WIFI_MODE_STA) {
      if (WiFi.mode(WIFI_MODE_STA)) {
        logger.info("Switched WiFi mode to STA only");
      } else {
        logger.warn("Failed to switch WiFi mode to STA only");
      }
    }
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    staConnected_ = false;
    setStatusLedMode(StatusLedMode::SlowBlink);
    logger.warn("STA disconnected, reconnecting");
    if (WiFi.getMode() != WIFI_MODE_STA) {
      dnsServer_.start(53, "*", WiFi.softAPIP());
      logger.info("Captive DNS started");
    }
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
      ipAddress_.fromString(getConfiguredIpAddress().c_str()) &&
      netmask_.fromString(getConfiguredNetmask().c_str());
  if (valid) {
    if (!gatewayValue.empty() &&
        !gateway_.fromString(gatewayValue.c_str())) {
      logger.warn("Invalid gateway configured, using 0.0.0.0");
      gateway_ = IPAddress(0, 0, 0, 0);
    } else if (gatewayValue.empty()) {
      gateway_ = IPAddress(0, 0, 0, 0);
    }

    bool dns1IsValid = true;
    if (!dns1Value.empty())
      dns1IsValid = dns1_.fromString(dns1Value.c_str());
    if (!dns1IsValid) logger.warn("Invalid DNS1 configured, ignoring");

    bool dns2IsValid = true;
    if (!dns2Value.empty())
      dns2IsValid = dns2_.fromString(dns2Value.c_str());
    if (!dns2IsValid) logger.warn("Invalid DNS2 configured, ignoring");

    const IPAddress dns1 =
        (dns1Value.empty() || !dns1IsValid)
            ? (gatewayValue.empty() ? IPAddress(0, 0, 0, 0) : gateway_)
            : dns1_;
    const IPAddress dns2 =
        (dns2Value.empty() || !dns2IsValid) ? IPAddress(0, 0, 0, 0) : dns2_;
    WiFi.config(ipAddress_, gateway_, netmask_, dns1, dns2);
    logger.info(
        std::string("Static IP configured: ") +
        std::string(ipAddress_.toString().c_str()) + ", Gateway: " +
        std::string(gateway_.toString().c_str()) + ", DNS1: " +
        std::string(dns1.toString().c_str()) +
        (dns2Value.empty()
             ? ""
             : ", DNS2: " + std::string(dns2.toString().c_str())));
  } else {
    logger.warn("Invalid static IP/netmask config, falling back to DHCP");
  }
}
