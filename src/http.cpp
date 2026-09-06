#include "http.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "api/adc_api.hpp"
#include "api/commands_api.hpp"
#include "api/config_api.hpp"
#include "api/cron_api.hpp"
#include "api/devices_api.hpp"
#include "api/logs_api.hpp"
#include "api/metrics_api.hpp"
#include "api/status_api.hpp"
#include "api/values_api.hpp"
#include "command_manager.hpp"
#include "http_utils.hpp"
#include "logger.hpp"
#include "main.hpp"
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
extern const char upgrade_html_start[] asm("_binary_upgrade_html_start");

esp_err_t handleCommonCss(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/css", common_css_start);
  return ESP_OK;
}

esp_err_t handleCommonJs(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "application/javascript",
                          common_js_start);
  return ESP_OK;
}

esp_err_t handleRoot(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", root_html_start);
  return ESP_OK;
}

esp_err_t handleUpgradePage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", upgrade_html_start);
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
  RegisterUri("/upgrade", HTTP_GET, handleUpgradePage);

  static ConfigApi config_api;
  config_api.registerHandlers(configServer);

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
