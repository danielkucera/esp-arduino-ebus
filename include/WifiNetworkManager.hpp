#pragma once

#include <DNSServer.h>
#include <WiFi.h>

#include <string>

class ConfigManager;

class WifiNetworkManager {
 public:
  void begin(ConfigManager* configManager);

  uint32_t getLastConnect() const;
  int getReconnectCount() const;
  bool isStaConnected() const;
  bool isCaptivePortalActive() const;
  bool isStaticIpEnabled() const;
  std::string getConfiguredIpAddress() const;
  std::string getConfiguredGateway() const;
  std::string getConfiguredNetmask() const;
  std::string getConfiguredDns1() const;
  std::string getConfiguredDns2() const;

 private:
  enum class StatusLedMode : uint8_t { SlowBlink = 0, SolidOn = 1 };

  static WifiNetworkManager* instance_;
  static void dnsTaskEntry(void* arg);
  void dnsTaskLoop();
  static void statusLedTaskEntry(void* arg);
  void statusLedTaskLoop();
  void initStatusLed();
  void setStatusLedMode(StatusLedMode mode);
  static void onWiFiEventStatic(WiFiEvent_t event, WiFiEventInfo_t info);
  void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
  void configureStaticIpIfEnabled();

  DNSServer dnsServer_;
  ConfigManager* configManager_ = nullptr;
  IPAddress ipAddress_;
  IPAddress gateway_;
  IPAddress netmask_;
  IPAddress dns1_;
  IPAddress dns2_;
  uint32_t lastConnect_ = 0;
  int reconnectCount_ = 0;
  bool staConnected_ = false;
  bool staConfigured_ = false;
  TaskHandle_t dnsTaskHandle_ = nullptr;
  TaskHandle_t statusLedTaskHandle_ = nullptr;
  volatile StatusLedMode statusLedMode_ = StatusLedMode::SlowBlink;
};
