#include "api/metrics_api.hpp"

#if defined(EBUS_INTERNAL)

#include "ebus_accessor.hpp"
#include "http.hpp"
#include "http_utils.hpp"

namespace {

// cppcheck-suppress syntaxError
extern const char metrics_html_start[] asm("_binary_metrics_html_start");

}  // namespace

MetricsApi* MetricsApi::instance_ = nullptr;

MetricsApi::MetricsApi() { instance_ = this; }

bool MetricsApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  RegisterUri("/metrics", HTTP_GET, handleMetricsPage);
  RegisterUri("/api/v1/metrics", HTTP_GET, handleMetrics);
  RegisterUri("/api/v1/metrics/reset", HTTP_POST, handleMetricsReset);

  return true;
}

esp_err_t MetricsApi::handleMetricsPage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", metrics_html_start);
  return ESP_OK;
}

esp_err_t MetricsApi::handleMetrics(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  getEbusController().fetchMetrics([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t MetricsApi::handleMetricsReset(httpd_req_t* req) {
  getEbusController().resetMetrics();
  HttpUtils::sendSuccessResponse(req, "reset");
  return ESP_OK;
}

#endif
