#include "http.hpp"

#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>
#include <string>
#include <vector>

#include "api/adc_api.hpp"
#include "api/commands_api.hpp"
#include "api/cron_api.hpp"
#include "api/devices_api.hpp"
#include "api/logs_api.hpp"
#include "api/metrics_api.hpp"
#include "api/status_api.hpp"
#include "api/values_api.hpp"
#include "config_manager.hpp"
#include "ebus_accessor.hpp"
#include "http_utils.hpp"
#include "logger.hpp"
#include "main.hpp"
#include "mqtt.hpp"
#include "mqtt_ha.hpp"
#include "wifi_network_manager.hpp"

static httpd_handle_t configServer = nullptr;
static bool fallbackHandlersRegistered = false;

namespace {
// cppcheck-suppress syntaxError
extern const char common_css_start[] asm("_binary_common_css_start");
// cppcheck-suppress syntaxError
extern const char common_js_start[] asm("_binary_common_js_start");

// cppcheck-suppress syntaxError
extern const char root_html_start[] asm("_binary_root_html_start");

// cppcheck-suppress syntaxError
extern const char config_html_start[] asm("_binary_config_html_start");
// cppcheck-suppress syntaxError
extern const char upgrade_html_start[] asm("_binary_upgrade_html_start");

void sendStatic(httpd_req_t* req, const char* contentType, const char* data) {
  HttpUtils::sendResponse(req, "200 OK", contentType, data);
}

esp_err_t handleCommonCss(httpd_req_t* req) {
  sendStatic(req, "text/css", common_css_start);
  return ESP_OK;
}

esp_err_t handleCommonJs(httpd_req_t* req) {
  sendStatic(req, "application/javascript", common_js_start);
  return ESP_OK;
}

esp_err_t handleRoot(httpd_req_t* req) {
  sendStatic(req, "text/html", root_html_start);
  return ESP_OK;
}

esp_err_t handleConfigPage(httpd_req_t* req) {
  sendStatic(req, "text/html", config_html_start);
  return ESP_OK;
}

esp_err_t handleWifiScan(httpd_req_t* req) {
  wifi_scan_config_t scanConfig = {};
  scanConfig.show_hidden = true;
  scanConfig.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scanConfig.scan_time.active.min = 0;
  scanConfig.scan_time.active.max = 0;
  scanConfig.scan_time.passive = 100;

  esp_err_t err = esp_wifi_scan_start(&scanConfig, true);
  if (err != ESP_OK) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "wifi_scan",
                                 "WiFi scan failed");
    return ESP_OK;
  }

  uint16_t apCount = 0;
  esp_wifi_scan_get_ap_num(&apCount);

  // Cap the number of records we read to avoid heap-allocating for every AP.
  // ESP32-C3 scans rarely see more than ~30 visible APs; 64 gives headroom
  // without a heap allocation (replaces std::vector<wifi_ap_record_t>).
  static constexpr size_t max_scan_aps = 64;
  size_t scan_count = std::min(static_cast<uint16_t>(max_scan_aps), apCount);
  static std::array<wifi_ap_record_t, max_scan_aps> aps{};
  apCount = static_cast<uint16_t>(scan_count);
  esp_wifi_scan_get_ap_records(&apCount, aps.data());

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
      char bssidStr[18];
      snprintf(bssidStr, sizeof(bssidStr), "%02x:%02x:%02x:%02x:%02x:%02x",
               ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4],
               ap.bssid[5]);
      writer.writeField("bssid", bssidStr);
      writer.writeField("rssi", ap.rssi);
      writer.writeField("channel", ap.primary);

      const char* authMode = "UNKNOWN";
      switch (ap.authmode) {
        case WIFI_AUTH_OPEN:
          authMode = "OPEN";
          break;
        case WIFI_AUTH_WEP:
          authMode = "WEP";
          break;
        case WIFI_AUTH_WPA_PSK:
          authMode = "WPA_PSK";
          break;
        case WIFI_AUTH_WPA2_PSK:
          authMode = "WPA2_PSK";
          break;
        case WIFI_AUTH_WPA_WPA2_PSK:
          authMode = "WPA_WPA2_PSK";
          break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
          authMode = "WPA2_ENTERPRISE";
          break;
        case WIFI_AUTH_WPA3_PSK:
          authMode = "WPA3_PSK";
          break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
          authMode = "WPA2_WPA3_PSK";
          break;
        default:
          break;
      }
      writer.writeField("authMode", authMode);
    }
  }
  httpd_resp_send_chunk(req, nullptr, 0);
  esp_wifi_clear_ap_list();
  return ESP_OK;
}

esp_err_t handleUpgradePage(httpd_req_t* req) {
  sendStatic(req, "text/html", upgrade_html_start);
  return ESP_OK;
}

esp_err_t handleRestart(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", "Restarting...");
  vTaskDelay(pdMS_TO_TICKS(500));
  restart();
  return ESP_OK;
}

esp_err_t handleNotFound(httpd_req_t* req) {
  if (!WifiNetworkManager::isStaConnected() &&
      WifiNetworkManager::getMode() != WIFI_MODE_STA) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Location", "/config");
    HttpUtils::applyCustomHeaders(req);
    httpd_resp_send(req, "", 0);
    return ESP_OK;
  }

  HttpUtils::sendResponse(req, "404 Not Found", "text/plain", "Not found");
  return ESP_OK;
}
}  // namespace

httpd_handle_t GetHttpServer() { return configServer; }

bool RegisterUri(const char* uri, httpd_method_t method,
                 esp_err_t (*handler)(httpd_req_t*)) {
  if (configServer == nullptr) {
    logger.error(std::string("HTTP server not started; cannot register ") +
                 uri);
    return false;
  }
  return HttpUtils::registerRoute(configServer, uri, method, handler);
}

void SetupHttpHandlers() {
  if (configServer != nullptr) return;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 64;
  config.stack_size = 8192;
  config.lru_purge_enable = true;
  config.max_open_sockets = 2;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;

  if (httpd_start(&configServer, &config) != ESP_OK) {
    logger.error("Failed to start HTTP server");
    return;
  }

  RegisterUri("/common.css", HTTP_GET, handleCommonCss);
  RegisterUri("/common.js", HTTP_GET, handleCommonJs);
  RegisterUri("/", HTTP_GET, handleRoot);
  RegisterUri("/config", HTTP_GET, handleConfigPage);
  RegisterUri("/upgrade", HTTP_GET, handleUpgradePage);
  RegisterUri("/api/v1/wifi/scan", HTTP_POST, handleWifiScan);

  static StatusApi status_api;
  status_api.registerHandlers(configServer);

  static AdcApi adc_api(adc);
  adc_api.registerHandlers(configServer);

#if defined(EBUS_INTERNAL)
  static CommandsApi commands_api(commandManager);
  commands_api.registerHandlers(configServer);

  static CronApi cron_api(cron);
  cron_api.registerHandlers(configServer);

  static ValuesApi values_api(commandManager);
  values_api.registerHandlers(configServer);

  static DevicesApi devices_api;
  devices_api.registerHandlers(configServer);

  static MetricsApi metrics_api;
  metrics_api.registerHandlers(configServer);

  static LogsApi logs_api(logger);
  logs_api.registerHandlers(configServer);

#endif

  RegisterUri("/restart", HTTP_GET, handleRestart);
}  // namespace

void SetupHttpFallbackHandlers() {
  if (configServer == nullptr || fallbackHandlersRegistered) return;
  RegisterUri("/*", HTTP_GET, handleNotFound);
  RegisterUri("/*", HTTP_POST, handleNotFound);
  fallbackHandlersRegistered = true;
}
