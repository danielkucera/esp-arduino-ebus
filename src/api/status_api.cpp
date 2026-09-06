#include "api/status_api.hpp"

#include "ebus_accessor.hpp"
#include "http.hpp"
#include "http_utils.hpp"
#include "main.hpp"

namespace {

// cppcheck-suppress syntaxError
extern const char status_html_start[] asm("_binary_status_html_start");

}  // namespace

StatusApi* StatusApi::instance_ = nullptr;

StatusApi::StatusApi() { instance_ = this; }

bool StatusApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  RegisterUri("/status", HTTP_GET, handleStatusPage);
  RegisterUri("/api/v1/status", HTTP_GET, handleStatus);

#if defined(EBUS_INTERNAL)
  RegisterUri("/api/v1/status/app", HTTP_GET, handleStatusApp);
  RegisterUri("/api/v1/status/lib", HTTP_GET, handleStatusLib);
#endif

  return true;
}

esp_err_t StatusApi::handleStatusPage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", status_html_start);
  return ESP_OK;
}

esp_err_t StatusApi::handleStatus(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  fetchStatus([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

#if defined(EBUS_INTERNAL)
esp_err_t StatusApi::handleStatusApp(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  fetchAppStatus([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t StatusApi::handleStatusLib(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  getEbusController().fetchStatus([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}
#endif
