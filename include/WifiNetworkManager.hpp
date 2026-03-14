#pragma once

#include "DNSServer.h"

#include <string>
#include <esp_wifi.h>


class ConfigManager;

class WifiNetworkManager {
 public:
  WifiNetworkManager() = delete;

  static void begin(ConfigManager* configManager);

  static uint32_t getLastConnect();
  static int getReconnectCount();
  static wifi_mode_t getMode();
  static bool isStaConnected();
  static bool isCaptivePortalActive();
  static bool isStaticIpEnabled();
  static std::string getConfiguredIpAddress();
  static std::string getConfiguredGateway();
  static std::string getConfiguredNetmask();
  static std::string getConfiguredDns1();
  static std::string getConfiguredDns2();

  static IPAddress localIP();
  static IPAddress gatewayIP();
  static IPAddress subnetMask();
  static IPAddress dnsIP(uint8_t index);

  static int32_t RSSI();
  static std::string SSID();
  static std::string BSSIDstr();
  static int32_t channel();
  static const char* getHostname();
  static std::string macAddress();

  static void handle_event(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);

 private:
  enum class StatusLedMode : uint8_t { SlowBlink = 0, SolidOn = 1 };

  static void dnsTaskEntry(void* arg);
  static void dnsTaskLoop();
  static void statusLedTaskEntry(void* arg);
  static void statusLedTaskLoop();
  static void initStatusLed();
  static void setStatusLedMode(StatusLedMode mode);
  static void configureStaticIpIfEnabled();

  static DNSServer dnsServer_;
  static ConfigManager* configManager_;
  static IPAddress ipAddress_;
  static IPAddress gateway_;
  static IPAddress netmask_;
  static IPAddress dns1_;
  static IPAddress dns2_;
  static uint32_t lastConnect_;
  static int reconnectCount_;
  static bool staConnected_;
  static bool staConfigured_;
  static TaskHandle_t dnsTaskHandle_;
  static TaskHandle_t statusLedTaskHandle_;
  static volatile StatusLedMode statusLedMode_;
  static esp_netif_t* staNetif_;
  static esp_netif_t* apNetif_;
};
