#pragma once

// Isolated ESP-IDF boundary for tests of the real WifiNetworkManager.
// These mocks are private to the wifi_manager_* targets.
#include <arpa/inet.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <freertos/FreeRTOS.h>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_INVALID_STATE = 0x103;
inline const char* esp_err_to_name(esp_err_t) { return "mock error"; }
using esp_event_base_t = const char*;
inline constexpr char WIFI_EVENT[] = "WIFI_EVENT";
inline constexpr char IP_EVENT[] = "IP_EVENT";
constexpr int ESP_EVENT_ANY_ID = -1;
constexpr int WIFI_EVENT_WIFI_READY = 0;
constexpr int IP_EVENT_STA_GOT_IP = 0;  // Deliberate cross-family overlap.
constexpr int WIFI_EVENT_STA_DISCONNECTED = 5;
inline esp_err_t esp_event_loop_create_default() { return ESP_OK; }
inline esp_err_t esp_event_handler_instance_register(
    esp_event_base_t, int, void (*)(void*, esp_event_base_t, int32_t, void*),
    void*, void*) { return ESP_OK; }

struct esp_ip4_addr_t { uint32_t addr = 0; };
using ip4_addr_t = esp_ip4_addr_t;
struct esp_netif_ip_info_t { esp_ip4_addr_t ip, netmask, gw; };
struct esp_netif_t {};
struct ip_event_got_ip_t { esp_netif_ip_info_t ip_info; };
enum esp_netif_dns_type_t { ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP };
constexpr int ESP_IPADDR_TYPE_V4 = 0;
struct esp_netif_dns_info_t {
  struct { int type; struct { esp_ip4_addr_t ip4; } u_addr; } ip;
};
#define ESP_IP4TOADDR(a, b, c, d) htonl(((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
inline char* ip4addr_ntoa_r(const ip4_addr_t* ip, char* out, int size) {
  return const_cast<char*>(inet_ntop(AF_INET, &ip->addr, out, size));
}
inline esp_err_t esp_netif_init() { return ESP_OK; }
inline esp_netif_t* esp_netif_create_default_wifi_sta() {
  static esp_netif_t netif;
  return &netif;
}
inline esp_netif_t* esp_netif_create_default_wifi_ap() {
  static esp_netif_t netif;
  return &netif;
}
inline esp_err_t esp_netif_set_hostname(esp_netif_t*, const char*) { return ESP_OK; }
inline esp_err_t esp_netif_get_hostname(esp_netif_t*, const char** name) {
  *name = "host-test"; return ESP_OK;
}
inline esp_err_t esp_netif_get_ip_info(esp_netif_t*, esp_netif_ip_info_t*) { return ESP_FAIL; }
inline esp_err_t esp_netif_get_dns_info(esp_netif_t*, esp_netif_dns_type_t, esp_netif_dns_info_t*) { return ESP_FAIL; }
inline bool esp_netif_str_to_ip4(const char* in, esp_ip4_addr_t* out) {
  return inet_pton(AF_INET, in, &out->addr) == 1;
}
inline esp_err_t esp_netif_dhcpc_stop(esp_netif_t*) { return ESP_OK; }
inline esp_err_t esp_netif_set_ip_info(esp_netif_t*, const esp_netif_ip_info_t*) { return ESP_OK; }
inline esp_err_t esp_netif_set_dns_info(esp_netif_t*, esp_netif_dns_type_t, const esp_netif_dns_info_t*) { return ESP_OK; }

enum wifi_mode_t { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA };
enum wifi_ps_type_t { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM };
enum wifi_interface_t { WIFI_IF_STA, WIFI_IF_AP };
enum wifi_storage_t { WIFI_STORAGE_FLASH, WIFI_STORAGE_RAM };
enum wifi_scan_method_t { WIFI_FAST_SCAN, WIFI_ALL_CHANNEL_SCAN };
enum wifi_auth_mode_t { WIFI_AUTH_OPEN, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK };
constexpr int WIFI_CONNECT_AP_BY_SIGNAL = 0;
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
struct wifi_config_t {
  struct {
    uint8_t ssid[32]{}, password[64]{}, bssid[6]{};
    bool bssid_set = false;
    wifi_scan_method_t scan_method = WIFI_FAST_SCAN;
    int sort_method = 0;
    struct { wifi_auth_mode_t authmode; } threshold{};
  } sta;
  struct {
    uint8_t ssid[32]{}, password[64]{};
    uint8_t ssid_len = 0, max_connection = 0, channel = 0;
    wifi_auth_mode_t authmode = WIFI_AUTH_OPEN;
  } ap;
};
struct wifi_ap_record_t {
  uint8_t ssid[33]{}, bssid[6]{};
  int rssi = 0, primary = 0;
};
struct esp_timer_create_args_t {
  void (*callback)(void*);
  void* arg;
  int dispatch_method;
  const char* name;
  bool skip_unhandled_events;
};
struct MockTimer {
  void (*callback)(void*) = nullptr;
  void* arg = nullptr;
  bool active = false;
  uint64_t delay_us = 0;
};
using esp_timer_handle_t = MockTimer*;
constexpr int ESP_TIMER_TASK = 0;
namespace wifi_mock {
inline wifi_mode_t mode = WIFI_MODE_NULL;
inline wifi_ps_type_t power_save = WIFI_PS_MIN_MODEM;
inline wifi_config_t sta_config{}, ap_config{};
inline esp_err_t connect_result = ESP_OK, mode_result = ESP_OK;
inline esp_err_t get_ps_result = ESP_OK, get_config_result = ESP_OK;
inline int connect_calls = 0, mode_calls = 0;
inline int ps_calls = 0;
inline wifi_mode_t initial_connect_mode = WIFI_MODE_NULL;
inline wifi_ps_type_t initial_connect_power_save = WIFI_PS_MIN_MODEM;
inline MockTimer timer;
}
inline esp_err_t esp_wifi_init(const wifi_init_config_t*) { return ESP_OK; }
inline esp_err_t esp_wifi_set_storage(wifi_storage_t) { return ESP_OK; }
inline esp_err_t esp_wifi_start() { return ESP_OK; }
inline esp_err_t esp_wifi_set_mode(wifi_mode_t mode) {
  ++wifi_mock::mode_calls;
  if (wifi_mock::mode_result == ESP_OK) wifi_mock::mode = mode;
  return wifi_mock::mode_result;
}
inline esp_err_t esp_wifi_get_mode(wifi_mode_t* mode) { *mode = wifi_mock::mode; return ESP_OK; }
inline esp_err_t esp_wifi_set_config(wifi_interface_t iface, const wifi_config_t* config) {
  (iface == WIFI_IF_STA ? wifi_mock::sta_config : wifi_mock::ap_config) = *config;
  return ESP_OK;
}
inline esp_err_t esp_wifi_get_config(wifi_interface_t, wifi_config_t* config) {
  *config = wifi_mock::sta_config; return wifi_mock::get_config_result;
}
inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t mode) {
  ++wifi_mock::ps_calls; wifi_mock::power_save = mode; return ESP_OK;
}
inline esp_err_t esp_wifi_get_ps(wifi_ps_type_t* mode) { *mode = wifi_mock::power_save; return wifi_mock::get_ps_result; }
inline esp_err_t esp_wifi_connect() {
  if (wifi_mock::initial_connect_mode == WIFI_MODE_NULL) {
    wifi_mock::initial_connect_mode = wifi_mock::mode;
    wifi_mock::initial_connect_power_save = wifi_mock::power_save;
  }
  ++wifi_mock::connect_calls; return wifi_mock::connect_result;
}
inline esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*) { return ESP_FAIL; }
inline esp_err_t esp_wifi_get_mac(wifi_interface_t, uint8_t*) { return ESP_FAIL; }
inline esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* timer) {
  wifi_mock::timer.callback = args->callback;
  wifi_mock::timer.arg = args->arg;
  *timer = &wifi_mock::timer; return ESP_OK;
}
inline esp_err_t esp_timer_stop(esp_timer_handle_t timer) { timer->active = false; return ESP_OK; }
inline esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t delay) {
  timer->active = true; timer->delay_us = delay; return ESP_OK;
}
inline int64_t esp_timer_get_time() { return 123456789; }

using gpio_num_t = int;
constexpr int GPIO_MODE_OUTPUT = 1, GPIO_PULLDOWN_DISABLE = 0;
constexpr int GPIO_PULLUP_DISABLE = 0, GPIO_INTR_DISABLE = 0;
struct gpio_config_t { uint64_t pin_bit_mask; int mode, pull_down_en, pull_up_en, intr_type; };
inline esp_err_t gpio_config(const gpio_config_t*) { return ESP_OK; }
inline esp_err_t gpio_set_level(gpio_num_t, int) { return ESP_OK; }
inline esp_err_t mdns_init() { return ESP_OK; }
inline esp_err_t mdns_hostname_set(const char*) { return ESP_OK; }
inline esp_err_t mdns_instance_name_set(const char*) { return ESP_OK; }
inline esp_err_t mdns_service_add(const char*, const char*, const char*, int, const void*, int) { return ESP_OK; }
