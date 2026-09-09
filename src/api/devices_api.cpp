#include "api/devices_api.hpp"

#if defined(EBUS_INTERNAL)

#include "ebus_accessor.hpp"
#include "http.hpp"
#include "http_utils.hpp"

namespace {

// cppcheck-suppress syntaxError
extern const char devices_html_start[] asm("_binary_devices_html_start");

}  // namespace

DevicesApi* DevicesApi::instance_ = nullptr;

DevicesApi::DevicesApi() { instance_ = this; }

bool DevicesApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  RegisterUri("/devices", HTTP_GET, handleDevicesPage);
  RegisterUri("/api/v1/devices", HTTP_GET, handleDevices);
  RegisterUri("/api/v1/devices/scan", HTTP_POST, handleDevicesScan);
  RegisterUri("/api/v1/devices/scan/full", HTTP_POST, handleDevicesScanFull);

  return true;
}

esp_err_t DevicesApi::handleDevicesPage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", devices_html_start);
  return ESP_OK;
}

esp_err_t DevicesApi::handleDevices(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  getEbusController().fetchDevices([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t DevicesApi::handleDevicesScan(httpd_req_t* req) {
  getEbusController().scanObservedDevices();
  HttpUtils::sendSuccessResponse(req, "scan", "initiated");
  return ESP_OK;
}

esp_err_t DevicesApi::handleDevicesScanFull(httpd_req_t* req) {
  getEbusController().initFullScan(true);
  HttpUtils::sendSuccessResponse(req, "scan_full", "initiated");
  return ESP_OK;
}

#endif