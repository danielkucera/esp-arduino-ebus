#pragma once

#include <cstring>

#include "esp_system.h"

typedef enum {
  WIFI_MODE_NULL = 0,
  WIFI_MODE_STA,
  WIFI_MODE_AP,
  WIFI_MODE_APSTA,
} wifi_mode_t;

typedef enum {
  WIFI_SCAN_TYPE_ACTIVE = 0,
  WIFI_SCAN_TYPE_PASSIVE,
} wifi_scan_type_t;

typedef enum {
  WIFI_AUTH_OPEN = 0,
  WIFI_AUTH_WEP,
  WIFI_AUTH_WPA_PSK,
  WIFI_AUTH_WPA2_PSK,
  WIFI_AUTH_WPA_WPA2_PSK,
  WIFI_AUTH_WPA2_ENTERPRISE,
  WIFI_AUTH_WPA3_PSK,
  WIFI_AUTH_WPA2_WPA3_PSK,
} wifi_auth_mode_t;

typedef struct {
  uint32_t min;
  uint32_t max;
} wifi_active_scan_time_t;

typedef struct {
  wifi_scan_type_t scan_type;
  bool show_hidden;
  union {
    struct {
      wifi_active_scan_time_t active;
      uint32_t passive;
    } scan_time;
  };
} wifi_scan_config_t;

typedef struct {
  uint8_t ssid[32];
  uint8_t bssid[6];
  int8_t rssi;
  uint8_t primary;
  wifi_auth_mode_t authmode;
} wifi_ap_record_t;

inline esp_err_t host_wifi_scan_start_result = ESP_OK;
inline uint16_t host_wifi_scan_ap_count = 0;
inline wifi_ap_record_t host_wifi_scan_records[64] = {};
inline uint32_t host_wifi_scan_clear_count = 0;

inline esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*, bool) {
  return host_wifi_scan_start_result;
}
inline esp_err_t esp_wifi_scan_get_ap_num(uint16_t* count) {
  if (count != nullptr) *count = host_wifi_scan_ap_count;
  return ESP_OK;
}
inline esp_err_t esp_wifi_scan_get_ap_records(uint16_t* count,
                                              wifi_ap_record_t* records) {
  if (count == nullptr || records == nullptr) return ESP_FAIL;
  const uint16_t available =
      *count < host_wifi_scan_ap_count ? *count : host_wifi_scan_ap_count;
  std::memcpy(records, host_wifi_scan_records,
              available * sizeof(wifi_ap_record_t));
  *count = available;
  return ESP_OK;
}
inline esp_err_t esp_wifi_clear_ap_list() {
  ++host_wifi_scan_clear_count;
  return ESP_OK;
}

#ifndef CONFIG_LWIP_MAX_SOCKETS
#define CONFIG_LWIP_MAX_SOCKETS 16
#endif
