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

  static void begin(ConfigManager* config_manager);

  static uint32_t getLastConnect();
  static int getReconnectCount();
  static wifi_mode_t getMode();
  static bool isStaConnected();
  static std::string_view getIpAddress();
  static void setStaIpAssignedCallback(
      void (*callback)(const std::string& ip_address));
  static bool isStaticIpEnabled();

  static std::string getConfiguredIpAddress();
  static std::string getConfiguredGateway();
  static std::string getConfiguredNetmask();
  static std::string getConfiguredDns1();
  static std::string getConfiguredDns2();

  static bool getStaIpInfo(esp_netif_ip_info_t* out_info);
  static bool getDnsIp(uint8_t index, esp_ip4_addr_t* out_ip);
  static std::string_view ipToString(const esp_ip4_addr_t& ip);

  static int32_t rssi();
  static std::string_view ssid();
  static std::string_view bssidStr();
  static int32_t channel();
  static const char* getHostname();
  static std::string_view macAddress();
  static void setStatusLedPin(int pin);

  static void handleEvent(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data);

  static TaskHandle_t getStatusLedTaskHandle();

 private:
  enum class StatusLedMode : uint8_t { slow_blink = 0, solid_on = 1 };

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
  static int consecutive_failures_;

  static ConfigManager* config_manager_;
  static esp_ip4_addr_t ip_address_;
  static esp_ip4_addr_t gateway_;
  static esp_ip4_addr_t netmask_;
  static esp_ip4_addr_t dns1_;
  static esp_ip4_addr_t dns2_;
  static uint32_t last_connect_;
  static int reconnect_count_;
  static bool sta_connected_;
  static bool sta_configured_;
  static TaskHandle_t status_led_task_handle_;
  static volatile StatusLedMode status_led_mode_;
  static int status_led_pin_;
  static void (*sta_ip_assigned_callback_)(const std::string& ip_address);
  static esp_netif_t* sta_netif_;
  static esp_netif_t* ap_netif_;
};

// Renders the "wifi" status section. Colocated here because it reads
// exclusively live WifiNetworkManager state (no config snapshot needed).
// Writes fields only: the caller opens the named object scope.
void appendWifiStatus(ebus::detail::JsonWriter& writer);
