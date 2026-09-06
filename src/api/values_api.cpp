#include "api/values_api.hpp"

#if defined(EBUS_INTERNAL)

#include <cstdio>
#include <ebus/detail/json_reader.hpp>

#include "command_manager.hpp"
#include "ebus_accessor.hpp"
#include "http.hpp"
#include "http_utils.hpp"

namespace {

// cppcheck-suppress syntaxError
extern const char values_html_start[] asm("_binary_values_html_start");

}  // namespace

ValuesApi* ValuesApi::instance_ = nullptr;

ValuesApi::ValuesApi(CommandManager& command_manager)
    : command_manager_(command_manager) {
  instance_ = this;
}

bool ValuesApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  RegisterUri("/values", HTTP_GET, handleValuesPage);
  RegisterUri("/api/v1/values", HTTP_GET, handleValues);
  RegisterUri("/api/v1/values/write", HTTP_POST, handleValuesWrite);
  RegisterUri("/api/v1/values/read", HTTP_POST, handleValuesRead);

  return true;
}

esp_err_t ValuesApi::handleValuesPage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", values_html_start);
  return ESP_OK;
}

esp_err_t ValuesApi::handleValues(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  instance_->command_manager_.fetchValues([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t ValuesApi::handleValuesWrite(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "write",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();

  std::string_view body_sv = sr.jsonReader().remaining();
  ebus::detail::JsonReader reader(body_sv);
  std::string key;
  if (reader.findKey("key") &&
      reader.next() == ebus::detail::JsonReader::Token::string) {
    key = std::string(reader.value());
  }

  Command* command = instance_->command_manager_.findCommand(key);
  if (command == nullptr) {
    HttpUtils::sendErrorResponse(req, "404 Not Found", "write",
                                 "Key '" + key + "' not found");
    return ESP_OK;
  }

  ebus::Sequence valueBytes = command->getVectorFromJson(body_sv);
  if (!valueBytes.empty()) {
    ebus::Sequence fullWrite =
        ebus::makeSequence(command->getWriteCmd(instance_->command_manager_));
    fullWrite.append(valueBytes);
    getEbusController().enqueue(prio_send, fullWrite);
    command->setLast(0);
    HttpUtils::sendSuccessResponse(req, "write");
  } else {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "write",
                                 "Invalid value for key '" + key + "'");
  }
  return ESP_OK;
}

esp_err_t ValuesApi::handleValuesRead(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "read",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();

  ebus::detail::JsonReader reader(sr.jsonReader().remaining());
  std::string key;
  if (reader.findKey("key") &&
      reader.next() == ebus::detail::JsonReader::Token::string) {
    key = std::string(reader.value());
  }

  if (key.empty()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "read",
                                 "invalid json payload");
    return ESP_OK;
  }

  Command* command = instance_->command_manager_.findCommand(key);
  if (command != nullptr) {
    command->setLast(0);
    HttpUtils::sendSuccessResponse(req, "read", "requested");
  } else {
    HttpUtils::sendErrorResponse(req, "404 Not Found", "read",
                                 "Key '" + key + "' not found");
  }
  return ESP_OK;
}

#endif