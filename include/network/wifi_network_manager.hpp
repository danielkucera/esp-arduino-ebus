#pragma once

#include <esp_netif_types.h>
#include <esp_wifi.h>

#include <ebus/detail/json_writer.hpp>
#include <string>
#include <string_view>

class ConfigManager;

class WifiNetworkManager {
 public:
  WifiNetworkManager() = delete;

  static void begin(ConfigManager* configManager);

  static uint32_t getLastConnect();
  static int getReconnectCount();
  static wifi_mode_t getMode();
  static bool isStaConnected();
  static std::string_view getIpAddress();
  static void setStaIpAssignedCallback(
      void (*callback)(const std::string& ipAddress));
  static bool isStaticIpEnabled();
  
  static std::string getConfiguredIpAddress();
  static std::string getConfiguredGateway();
  static std::string getConfiguredNetmask();
  static std::string getConfiguredDns1();
  static std::string getConfiguredDns2();

  static bool getStaIpInfo(esp_netif_ip_info_t* outInfo);
  static bool getDnsIp(uint8_t index, esp_ip4_addr_t* outIp);
  static std::string_view ipToString(const esp_ip4_addr_t& ip);

  static int32_t RSSI();
  static std::string_view SSID();
  static std::string_view BSSIDstr();
  static int32_t channel();
  static const char* getHostname();
  static std::string_view macAddress();
  static void setStatusLedPin(int pin);

  static void handle_event(void* arg, esp_event_base_t event_base,
                           int32_t event_id, void* event_data);

  static TaskHandle_t getStatusLedTaskHandle();

 private:
  enum class StatusLedMode : uint8_t { SlowBlink = 0, SolidOn = 1 };

  static void statusLedTaskEntry(void* arg);
  static void statusLedTaskLoop();
  static void initStatusLed();
  static void setStatusLedMode(StatusLedMode mode);
  static void configureStaticIpIfEnabled();
  // One-shot diagnostics: after N consecutive STA failures the console is
  // likely the only channel left (no WiFi = no HTTP), so log the full WiFi
  // slice including the password (physical access implies full control
  // anyway). Counter resets on every successful connect.
  static void logWifiSlice();
  static int consecutiveFailures_;
  static constexpr int maxConsecutiveFailures = 5;

  static ConfigManager* configManager_;
  static esp_ip4_addr_t ipAddress_;
  static esp_ip4_addr_t gateway_;
  static esp_ip4_addr_t netmask_;
  static esp_ip4_addr_t dns1_;
  static esp_ip4_addr_t dns2_;
  static uint32_t lastConnect_;
  static int reconnectCount_;
  static bool staConnected_;
  static bool staConfigured_;
  static TaskHandle_t statusLedTaskHandle_;
  static volatile StatusLedMode statusLedMode_;
  static int statusLedPin_;
  static void (*staIpAssignedCallback_)(const std::string& ipAddress);
  static esp_netif_t* staNetif_;
  static esp_netif_t* apNetif_;
};

// Renders the "wifi" status section. Colocated here because it reads
// exclusively live WifiNetworkManager state (no config snapshot needed).
// Writes fields only: the caller opens the named object scope.
void appendWifiStatus(ebus::detail::JsonWriter& writer);
