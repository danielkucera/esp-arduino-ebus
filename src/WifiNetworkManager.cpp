#include "WifiNetworkManager.hpp"

#include <ESPmDNS.h>
#include <cctype>
#include <cstring>
#include <esp_wifi.h>

#include "ConfigManager.hpp"
#include "Logger.hpp"

WifiNetworkManager* WifiNetworkManager::instance_ = nullptr;

namespace {

String buildHostname(const String& source, const char* fallback) {
  String hostname = source;
  hostname.trim();
  if (hostname.isEmpty()) hostname = fallback;

  String sanitized;
  sanitized.reserve(hostname.length());
  for (size_t i = 0; i < hostname.length(); ++i) {
    const char c = hostname[i];
    if (std::isalnum(static_cast<unsigned char>(c))) {
      sanitized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (c == '-' || c == '_' || c == ' ') {
      sanitized += '-';
    }
  }

  while (sanitized.startsWith("-")) sanitized.remove(0, 1);
  while (sanitized.endsWith("-")) sanitized.remove(sanitized.length() - 1, 1);

  if (sanitized.isEmpty()) sanitized = "esp-ebus";
  if (sanitized.length() > 63) sanitized.remove(63);
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

  String apPassword = configManager_ != nullptr
                          ? configManager_->readString("apModePassword",
                                                       kDefaultApPassword)
                          : String(kDefaultApPassword);
  if (apPassword.isEmpty()) apPassword = kDefaultApPassword;
  const String configuredThingName =
      configManager_ != nullptr
          ? configManager_->readString("thingName", kDefaultHostname)
          : String(kDefaultHostname);
  const String hostname = buildHostname(configuredThingName, kDefaultHostname);

  WiFi.onEvent(onWiFiEventStatic);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(hostname.c_str());
  WiFi.mode(WIFI_AP_STA);
  if (MDNS.begin(hostname.c_str())) {
    logger.info("Hostname/mDNS: " + hostname + ".local");
  } else {
    logger.warn("mDNS start failed");
  }

  wifi_config_t apConfig{};
  std::strncpy(reinterpret_cast<char*>(apConfig.ap.ssid), kDefaultApSsid,
               sizeof(apConfig.ap.ssid) - 1);
  apConfig.ap.ssid_len = std::strlen(kDefaultApSsid);
  std::strncpy(reinterpret_cast<char*>(apConfig.ap.password), apPassword.c_str(),
               sizeof(apConfig.ap.password) - 1);
  apConfig.ap.max_connection = 4;
  apConfig.ap.channel = rand() % 13 + 1;
  apConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
  if (apPassword.length() < 8) apConfig.ap.authmode = WIFI_AUTH_OPEN;
  if (esp_wifi_set_config(WIFI_IF_AP, &apConfig) != ESP_OK) {
    logger.error("AP config apply failed");
  } else {
    logger.info("AP ready: " + String(kDefaultApSsid) + " (" +
                String(apConfig.ap.authmode == WIFI_AUTH_OPEN ? "open" : "wpa2") +
                ")");
  }
  dnsServer_.start(53, "*", WiFi.softAPIP());
  if (dnsTaskHandle_ == nullptr) {
    xTaskCreate(dnsTaskEntry, "dns_task", 4096, this, 1, &dnsTaskHandle_);
    logger.info("Captive DNS task started");
  }

  String staSsid = configManager_ != nullptr
                       ? configManager_->readString("wifiSsid", "ebus-test")
                       : String("ebus-test");
  String staPass = configManager_ != nullptr
                       ? configManager_->readString("wifiPassword", "lectronz")
                       : String("lectronz");
  staConfigured_ = staSsid.length() > 0;

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
      digitalWrite(STATUS_LED_PIN, HIGH);
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    ledOn = !ledOn;
    digitalWrite(STATUS_LED_PIN, ledOn ? HIGH : LOW);
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
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
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

String WifiNetworkManager::getConfiguredIpAddress() const {
  return configManager_ != nullptr ? configManager_->readString("ipAddress") : "";
}

String WifiNetworkManager::getConfiguredGateway() const {
  return configManager_ != nullptr ? configManager_->readString("gateway") : "";
}

String WifiNetworkManager::getConfiguredNetmask() const {
  return configManager_ != nullptr ? configManager_->readString("netmask") : "";
}

String WifiNetworkManager::getConfiguredDns1() const {
  return configManager_ != nullptr ? configManager_->readString("dns1") : "";
}

String WifiNetworkManager::getConfiguredDns2() const {
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
    lastConnect_ = millis();
    ++reconnectCount_;
    logger.info("STA connected, IP: " + WiFi.localIP().toString());
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

  const String gatewayValue = getConfiguredGateway();
  const String dns1Value = getConfiguredDns1();
  const String dns2Value = getConfiguredDns2();

  const bool valid = ipAddress_.fromString(getConfiguredIpAddress()) &&
                     netmask_.fromString(getConfiguredNetmask());
  if (valid) {
    if (!gatewayValue.isEmpty() && !gateway_.fromString(gatewayValue)) {
      logger.warn("Invalid gateway configured, using 0.0.0.0");
      gateway_ = IPAddress(0, 0, 0, 0);
    } else if (gatewayValue.isEmpty()) {
      gateway_ = IPAddress(0, 0, 0, 0);
    }

    bool dns1IsValid = true;
    if (!dns1Value.isEmpty()) dns1IsValid = dns1_.fromString(dns1Value);
    if (!dns1IsValid) logger.warn("Invalid DNS1 configured, ignoring");

    bool dns2IsValid = true;
    if (!dns2Value.isEmpty()) dns2IsValid = dns2_.fromString(dns2Value);
    if (!dns2IsValid) logger.warn("Invalid DNS2 configured, ignoring");

    const IPAddress dns1 =
        (dns1Value.isEmpty() || !dns1IsValid)
            ? (gatewayValue.isEmpty() ? IPAddress(0, 0, 0, 0) : gateway_)
            : dns1_;
    const IPAddress dns2 =
        (dns2Value.isEmpty() || !dns2IsValid) ? IPAddress(0, 0, 0, 0) : dns2_;
    WiFi.config(ipAddress_, gateway_, netmask_, dns1, dns2);
    logger.info("Static IP configured: " + ipAddress_.toString() +
                ", Gateway: " + gateway_.toString() +
                ", DNS1: " + dns1.toString() +
                (dns2Value.isEmpty() ? "" : ", DNS2: " + dns2.toString()));
  } else {
    logger.warn("Invalid static IP/netmask config, falling back to DHCP");
  }
}
