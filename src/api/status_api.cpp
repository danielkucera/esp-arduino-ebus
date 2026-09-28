#include "api/status_api.hpp"

#include "network/http.hpp"
#include "network/http_utils.hpp"

#if defined(EBUS_INTERNAL)
#include <esp_timer.h>

#if __has_include("app/build_info_gen.hpp")
#include "app/build_info_gen.hpp"
#endif

#include "app/ebus_accessor.hpp"
#include "app/mqtt.hpp"
#include "network/wifi_network_manager.hpp"
#include "system/device_status.hpp"
#include "system/logger.hpp"
#endif

namespace {

// cppcheck-suppress syntaxError
extern const char status_html_start[] asm("_binary_status_html_start");

}  // namespace

StatusApi::StatusApi() {}

bool StatusApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  RegisterUri("/status", HTTP_GET, handleStatusPage);
#if defined(EBUS_INTERNAL)
  RegisterUri("/api/v1/health", HTTP_GET, handleHealth);
#endif

  return true;
}

esp_err_t StatusApi::handleStatusPage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", status_html_start);
  return ESP_OK;
}

#if defined(EBUS_INTERNAL)

esp_err_t StatusApi::handleHealth(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  ebus::detail::JsonWriter writer([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });

  uint32_t total_messages = 0;
  uint32_t total_errors = 0;
  float error_rate = 0.0f;
  bool breaker_open = false;

  getEbusController().fetchMetrics([&](const ebus::Metrics& m) {
    total_messages = m.handler.messages_passive + m.handler.messages_active +
                     m.handler.messages_reactive;
    total_errors = m.handler.error_passive + m.handler.error_reactive +
                   m.handler.error_active;
    error_rate = m.handler.errorRate();
    breaker_open = m.scheduler.breaker_open;
  });

  {
    auto scope = writer.objectScope();
#if __has_include("app/build_info_gen.hpp")
    writer.writeField("firmware", AUTO_VERSION);
    writer.writeField("build", build_time_str);
#else
    writer.writeField("firmware", AUTO_VERSION);
    writer.writeField("build", __DATE__ " " __TIME__);
#endif
    writer.writeField("uptime",
                      static_cast<uint64_t>(esp_timer_get_time() / 1000ULL));
    writer.writeField("reset_code", DeviceStatus::resetCode());
    writer.writeField("rssi", WifiNetworkManager::RSSI());
    writer.writeField("wifi_reconnects",
                      WifiNetworkManager::getReconnectCount());
    writer.writeField("mqtt_connected", DeviceStatus::mqtt().isConnected());
    writer.writeField("error_rate_pct", error_rate);
    writer.writeField("breaker_open", breaker_open);
    writer.writeField("messages", total_messages);
    writer.writeField("errors", total_errors);
    {
      auto logger_scope = writer.objectScope("logger");
      writer.writeField("ring_overwrites", logger.getRingOverwriteCount());
      writer.writeField("print_drops", logger.getPrintDropCount());
    }
  }
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

#endif
