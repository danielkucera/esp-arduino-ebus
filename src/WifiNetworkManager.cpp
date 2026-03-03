#include "WifiNetworkManager.hpp"

#include <cstring>
#include <esp_wifi.h>

#include "ConfigManager.hpp"

WifiNetworkManager* WifiNetworkManager::instance_ = nullptr;

void WifiNetworkManager::begin(ConfigManager* configManager, const char* hostname,
                               const char* apSsid, const char* apPassword) {
  configManager_ = configManager;
  instance_ = this;

  WiFi.onEvent(onWiFiEventStatic);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(hostname);
  WiFi.mode(WIFI_AP_STA);

  wifi_config_t apConfig{};
  std::strncpy(reinterpret_cast<char*>(apConfig.ap.ssid), apSsid,
               sizeof(apConfig.ap.ssid) - 1);
  apConfig.ap.ssid_len = std::strlen(apSsid);
  std::strncpy(reinterpret_cast<char*>(apConfig.ap.password), apPassword,
               sizeof(apConfig.ap.password) - 1);
  apConfig.ap.max_connection = 4;
  apConfig.ap.channel = rand() % 13 + 1;
  apConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
  if (std::strlen(apPassword) < 8) apConfig.ap.authmode = WIFI_AUTH_OPEN;
  esp_wifi_set_config(WIFI_IF_AP, &apConfig);
  dnsServer_.start(53, "*", WiFi.softAPIP());
  if (dnsTaskHandle_ == nullptr) {
    xTaskCreate(dnsTaskEntry, "dns_task", 4096, this, 1, &dnsTaskHandle_);
  }

  String staSsid = readConfigValue("wifiSsid");
  String staPass = readConfigValue("wifiPassword");
  staConfigured_ = staSsid.length() > 0;

  if (!staConfigured_) return;

  configureStaticIpIfEnabled();

  wifi_config_t staConfig{};
  std::strncpy(reinterpret_cast<char*>(staConfig.sta.ssid), staSsid.c_str(),
               sizeof(staConfig.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(staConfig.sta.password), staPass.c_str(),
               sizeof(staConfig.sta.password) - 1);
  esp_wifi_set_config(WIFI_IF_STA, &staConfig);
  esp_wifi_connect();
}

uint32_t WifiNetworkManager::getLastConnect() const { return lastConnect_; }

int WifiNetworkManager::getReconnectCount() const { return reconnectCount_; }

bool WifiNetworkManager::isStaConnected() const { return staConnected_; }

bool WifiNetworkManager::isCaptivePortalActive() const {
  return WiFi.getMode() != WIFI_MODE_STA;
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
    if (WiFi.getMode() == WIFI_MODE_APSTA) {
      esp_wifi_set_mode(WIFI_MODE_STA);
      dnsServer_.stop();
    }
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    staConnected_ = false;
    if (staConfigured_) esp_wifi_connect();
  }
}

void WifiNetworkManager::configureStaticIpIfEnabled() {
  if (!isStaticIpEnabled()) return;

  bool valid = true;
  valid = valid && ipAddress_.fromString(getConfiguredIpAddress());
  valid = valid && netmask_.fromString(getConfiguredNetmask());
  valid = valid && gateway_.fromString(getConfiguredGateway());

  if (valid) WiFi.config(ipAddress_, gateway_, netmask_);
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
