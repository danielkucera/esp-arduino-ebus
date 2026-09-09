#include "api/logs_api.hpp"

#if defined(EBUS_INTERNAL)

#include <cstdint>
#include <cstdio>

#include "http.hpp"
#include "http_utils.hpp"

namespace {

// cppcheck-suppress syntaxError
extern const char logs_html_start[] asm("_binary_logs_html_start");

}  // namespace

LogsApi* LogsApi::instance_ = nullptr;

LogsApi::LogsApi(Logger& logger) : logger_(logger) { instance_ = this; }

bool LogsApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  RegisterUri("/logs", HTTP_GET, handleLogsPage);
  RegisterUri("/api/v1/logs", HTTP_GET, handleLogs);
  RegisterUri("/api/v1/logs/time-relation", HTTP_GET, handleLogsTimeRelation);

  return true;
}

esp_err_t LogsApi::handleLogsPage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", logs_html_start);
  return ESP_OK;
}

esp_err_t LogsApi::handleLogs(httpd_req_t* req) {
  uint64_t sinceMillis = 0;
  const size_t queryLen = httpd_req_get_url_query_len(req);
  if (queryLen > 0) {
    char queryBuf[256];
    if (queryLen + 1 > sizeof(queryBuf)) {
      httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, nullptr);
      return ESP_OK;
    }
    if (httpd_req_get_url_query_str(req, queryBuf, sizeof(queryBuf)) ==
        ESP_OK) {
      char sinceBuffer[32] = {0};
      if (httpd_query_key_value(queryBuf, "since", sinceBuffer,
                                sizeof(sinceBuffer)) == ESP_OK) {
        sinceMillis = std::strtoull(sinceBuffer, nullptr, 10);
      }
    }
  }
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  instance_->logger_.fetchLogs(
      [req](std::string_view chunk) {
        httpd_resp_send_chunk(req, chunk.data(), chunk.size());
      },
      sinceMillis);
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t LogsApi::handleLogsTimeRelation(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  instance_->logger_.fetchTimeRelation([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

#endif
