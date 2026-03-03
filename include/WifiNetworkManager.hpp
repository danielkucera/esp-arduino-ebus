#pragma once

#include <DNSServer.h>
#include <WiFi.h>

class ConfigManager;

class WifiNetworkManager {
 public:
  void begin(ConfigManager* configManager);

  uint32_t getLastConnect() const;
  int getReconnectCount() const;
  bool isStaConnected() const;
  bool isCaptivePortalActive() const;
  bool isStaticIpEnabled() const;
  String getConfiguredIpAddress() const;
  String getConfiguredGateway() const;
  String getConfiguredNetmask() const;

 private:
  static WifiNetworkManager* instance_;
  static void dnsTaskEntry(void* arg);
  void dnsTaskLoop();
  static void onWiFiEventStatic(WiFiEvent_t event, WiFiEventInfo_t info);
  void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
  void configureStaticIpIfEnabled();

  DNSServer dnsServer_;
  ConfigManager* configManager_ = nullptr;
  IPAddress ipAddress_;
  IPAddress gateway_;
  IPAddress netmask_;
  uint32_t lastConnect_ = 0;
  int reconnectCount_ = 0;
  bool staConnected_ = false;
  bool staConfigured_ = false;
  TaskHandle_t dnsTaskHandle_ = nullptr;
};
