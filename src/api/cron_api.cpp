#include "api/cron_api.hpp"

#if defined(EBUS_INTERNAL)

#include <ebus/detail/json_reader.hpp>

#include "cron.hpp"
#include "http.hpp"
#include "http_utils.hpp"

namespace {

// cppcheck-suppress syntaxError
extern const char cron_html_start[] asm("_binary_cron_html_start");

}  // namespace

CronApi* CronApi::instance_ = nullptr;

CronApi::CronApi(Cron& cron) : cron_(cron) { instance_ = this; }

bool CronApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  RegisterUri("/cron", HTTP_GET, handleCronPage);
  RegisterUri("/api/v1/cron", HTTP_GET, handleCron);
  RegisterUri("/api/v1/cron", HTTP_POST, handleCronSave);
  RegisterUri("/api/v1/cron/load", HTTP_POST, handleCronLoad);
  RegisterUri("/api/v1/cron/evaluate", HTTP_POST, handleCronEvaluate);

  return true;
}

esp_err_t CronApi::handleCronPage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", cron_html_start);
  return ESP_OK;
}

esp_err_t CronApi::handleCron(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  cron.fetchRules([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t CronApi::handleCronEvaluate(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();

  ebus::detail::JsonReader reader(sr.jsonReader().remaining());
  std::string parse_error;
  if (!HttpUtils::prepareJsonReaderForArray(reader, "", parse_error)) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate",
                                 parse_error);
    return ESP_OK;
  }

  std::string evalError;
  while (true) {
    std::string_view rule_sv = reader.rawValue();
    if (rule_sv.empty()) break;
    ebus::detail::JsonReader rule_reader(rule_sv);
    evalError = Cron::evaluate(rule_reader);
    if (!evalError.empty()) break;
  }

  if (!evalError.empty())
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate", evalError);
  else
    HttpUtils::sendSuccessResponse(req, "evaluate");

  return ESP_OK;
}

esp_err_t CronApi::handleCronSave(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "save",
                                 "Receive failed");
    return ESP_OK;
  }
  sr.endOfInput();
  int64_t bytes = cron.replaceRules(sr.jsonReader().remaining());
  if (bytes >= 0) {
    HttpUtils::sendSuccessResponse(
        req, "save", "successful",
        bytes > 0 ? "Saved " + std::to_string(bytes) + " bytes" : "");
  } else {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "save",
                                 "Save failed");
  }
  return ESP_OK;
}

esp_err_t CronApi::handleCronLoad(httpd_req_t* req) {
  int64_t bytes = cron.loadRules();
  if (bytes > 0) {
    HttpUtils::sendSuccessResponse(
        req, "load", "successful",
        "Loaded " + std::to_string(bytes) + " bytes");
  } else if (bytes < 0) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "load",
                                 "Load failed");
  } else {
    HttpUtils::sendSuccessResponse(req, "load", "no data");
  }
  return ESP_OK;
}

#endif