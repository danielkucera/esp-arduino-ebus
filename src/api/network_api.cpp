#include "api/network_api.hpp"

#if defined(EBUS_INTERNAL)

#include <esp_timer.h>
#include <esp_wifi.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <ebus/detail/json_writer.hpp>

#include "app/mqtt.hpp"
#include "config/app_config.hpp"
#include "network/http.hpp"
#include "network/http_utils.hpp"
#include "network/sntp.hpp"
#include "network/wifi_network_manager.hpp"
#include "system/device_status.hpp"
#include "system/system_monitor.hpp"

NetworkApi::NetworkApi() {}

bool NetworkApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  registerUri("/api/v1/network", HTTP_GET, handleNetwork);
  registerUri("/api/v1/network/wifi/scan", HTTP_POST, handleWifiScan);

  return true;
}

esp_err_t NetworkApi::handleNetwork(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  ebus::detail::JsonWriter writer([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  {
    auto scope = writer.objectScope();
    {
      auto wifi = writer.objectScope("wifi");
      appendWifiStatus(writer);
    }
    {
      auto mqtt = writer.objectScope("mqtt");
      appendMqttStatus(writer, DeviceStatus::config().mqtt);
      writer.writeField("published", DeviceStatus::mqtt().getPublishedCount());
      writer.writeField("publish_failed",
                        DeviceStatus::mqtt().getPublishFailedCount());
      writer.writeField("queue_dropped",
                        DeviceStatus::mqtt().getQueueDropCount());
      writer.writeField("connects", DeviceStatus::mqtt().getConnectCount());
    }
    {
      auto sockets = writer.objectScope("sockets");
      int detected = 0;
      int connected = 0;
      DeviceStatus::monitor().getSocketStatus(detected, connected);
      writer.writeField("detected", detected);
      writer.writeField("connected", connected);
      writer.writeField("max", CONFIG_LWIP_MAX_SOCKETS);
    }
    {
      auto sntp = writer.objectScope("sntp");
      appendSntpStatus(writer, DeviceStatus::config().sntp);
    }
  }
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t NetworkApi::handleWifiScan(httpd_req_t* req) {
  wifi_scan_config_t scan_config = {};
  scan_config.show_hidden = true;
  scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scan_config.scan_time.active.min = 0;
  scan_config.scan_time.active.max = 0;
  scan_config.scan_time.passive = 100;

  esp_err_t err = esp_wifi_scan_start(&scan_config, true);
  if (err != ESP_OK) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "wifi_scan",
                                 "WiFi scan failed");
    return ESP_OK;
  }

  uint16_t ap_count = 0;
  esp_wifi_scan_get_ap_num(&ap_count);

  static constexpr size_t max_scan_aps = 64;
  size_t scan_count = std::min(static_cast<uint16_t>(max_scan_aps), ap_count);
  static std::array<wifi_ap_record_t, max_scan_aps> aps{};
  ap_count = static_cast<uint16_t>(scan_count);
  esp_wifi_scan_get_ap_records(&ap_count, aps.data());

  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  ebus::detail::JsonWriter writer([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });

  {
    auto array_scope = writer.arrayScope();
    for (size_t i = 0; i < scan_count; ++i) {
      const auto& ap = aps[i];
      auto obj_scope = writer.objectScope();
      char ssid_buf[33];
      size_t ssid_len = strnlen(reinterpret_cast<const char*>(ap.ssid), 32);
      memcpy(ssid_buf, ap.ssid, ssid_len);
      ssid_buf[ssid_len] = '\0';
      writer.writeField("ssid", ssid_buf);
      char bssid_str[18];
      snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x",
               ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4],
               ap.bssid[5]);
      writer.writeField("bssid", bssid_str);
      writer.writeField("rssi", ap.rssi);
      writer.writeField("channel", ap.primary);

      const char* auth_mode = "UNKNOWN";
      switch (ap.authmode) {
        case WIFI_AUTH_OPEN:
          auth_mode = "OPEN";
          break;
        case WIFI_AUTH_WEP:
          auth_mode = "WEP";
          break;
        case WIFI_AUTH_WPA_PSK:
          auth_mode = "WPA_PSK";
          break;
        case WIFI_AUTH_WPA2_PSK:
          auth_mode = "WPA2_PSK";
          break;
        case WIFI_AUTH_WPA_WPA2_PSK:
          auth_mode = "WPA_WPA2_PSK";
          break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
          auth_mode = "WPA2_ENTERPRISE";
          break;
        case WIFI_AUTH_WPA3_PSK:
          auth_mode = "WPA3_PSK";
          break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
          auth_mode = "WPA2_WPA3_PSK";
          break;
        default:
          break;
      }
      writer.writeField("authMode", auth_mode);
    }
  }
  httpd_resp_send_chunk(req, nullptr, 0);
  esp_wifi_clear_ap_list();
  return ESP_OK;
}

#endif
