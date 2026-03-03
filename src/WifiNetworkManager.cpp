#include "WifiNetworkManager.hpp"

#include <ESPmDNS.h>
#include <cstring>
#include <esp_wifi.h>

#include "ConfigManager.hpp"
#include "Logger.hpp"

WifiNetworkManager* WifiNetworkManager::instance_ = nullptr;

void WifiNetworkManager::begin(ConfigManager* configManager) {
  static constexpr const char* kDefaultHostname = "esp-eBus";
  static constexpr const char* kDefaultApSsid = "esp-eBus";
  static constexpr const char* kDefaultApPassword = "ebusebus";

  configManager_ = configManager;
  instance_ = this;

  String apPassword = readConfigValue("apModePassword", kDefaultApPassword);
  if (apPassword.isEmpty()) apPassword = kDefaultApPassword;

  WiFi.onEvent(onWiFiEventStatic);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(kDefaultHostname);
  WiFi.mode(WIFI_AP_STA);
  if (MDNS.begin(kDefaultHostname)) {
    logger.info("mDNS started: " + String(kDefaultHostname) + ".local");
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

  String staSsid = readConfigValue("wifiSsid", "ebus-test");
  String staPass = readConfigValue("wifiPassword", "lectronz");
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

bool WifiNetworkManager::isStaticIpEnabled() const {
  return readConfigBool("staticIPEnabled");
}

String WifiNetworkManager::getConfiguredIpAddress() const {
  return readConfigValue("ipAddress");
}

String WifiNetworkManager::getConfiguredGateway() const {
  return readConfigValue("gateway");
}

String WifiNetworkManager::getConfiguredNetmask() const {
  return readConfigValue("netmask");
}

void WifiNetworkManager::onWiFiEventStatic(WiFiEvent_t event,
                                           WiFiEventInfo_t info) {
  if (instance_ != nullptr) instance_->onWiFiEvent(event, info);
}

void WifiNetworkManager::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    staConnected_ = true;
    lastConnect_ = millis();
    ++reconnectCount_;
    logger.info("STA connected, IP: " + WiFi.localIP().toString());
    // Keep AP running in AP+STA mode; only disable captive DNS behavior.
    dnsServer_.stop();
    logger.info("Captive DNS stopped");
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    staConnected_ = false;
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

  bool valid = true;
  valid = valid && ipAddress_.fromString(getConfiguredIpAddress());
  valid = valid && netmask_.fromString(getConfiguredNetmask());
  valid = valid && gateway_.fromString(getConfiguredGateway());

  if (valid) {
    WiFi.config(ipAddress_, gateway_, netmask_);
    logger.info("Static IP configured: " + ipAddress_.toString());
  } else {
    logger.warn("Invalid static IP config, falling back to DHCP");
  }
}

bool WifiNetworkManager::readConfigBool(const char* key, bool fallback) const {
  if (configManager_ == nullptr) return fallback;
  return parseStoredBool(
      configManager_->readString(key, fallback ? "selected" : ""));
}

String WifiNetworkManager::readConfigValue(const char* key,
                                           const char* fallback) const {
  if (configManager_ == nullptr) return String(fallback);
  return configManager_->readString(key, fallback);
}

bool WifiNetworkManager::parseStoredBool(const String& value) {
  return value == "selected" || value == "true" || value == "1" ||
         value == "on";
}
