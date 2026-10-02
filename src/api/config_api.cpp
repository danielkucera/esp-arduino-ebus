#include "api/config_api.hpp"

#include <freertos/FreeRTOS.h>

#include "network/http.hpp"
#include "network/http_utils.hpp"

namespace {

// cppcheck-suppress syntaxError
extern const char config_html_start[] asm("_binary_config_html_start");

}  // namespace

ConfigApi::ConfigApi() {}

bool ConfigApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  RegisterUri("/config", HTTP_GET, handleConfigPage);

  return true;
}

esp_err_t ConfigApi::handleConfigPage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", config_html_start);
  return ESP_OK;
}
