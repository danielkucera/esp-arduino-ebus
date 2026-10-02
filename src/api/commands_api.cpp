#include "api/commands_api.hpp"

#if defined(EBUS_INTERNAL)

#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cerrno>
#include <cstdio>
#include <ebus/detail/json_reader.hpp>
#ifdef EBUS_COMMANDS_TMP_FILE_PATH
#include <unistd.h>
#endif

#include "app/command_manager.hpp"
#include "app/mqtt.hpp"
#include "app/mqtt_ha.hpp"
#include "network/http.hpp"
#include "network/http_utils.hpp"
#include "system/logger.hpp"

namespace {

// cppcheck-suppress syntaxError
extern const char commands_html_start[] asm("_binary_commands_html_start");

// Evaluate a JSON array of command objects. Returns empty string_view on
// success, or a non-empty error message on the first failing command.
std::string_view evaluateCommands(ebus::detail::JsonReader& reader) {
  std::string_view eval_error;
  bool header_seen = false;
  while (true) {
    std::string_view row_sv = reader.rawValue();
    if (row_sv.empty()) break;
    ebus::detail::JsonReader row_reader(row_sv);
    auto row_token = row_reader.next();

    if (row_token == ebus::detail::JsonReader::Token::array_start) {
      if (!header_seen) {
        header_seen = true;
        continue;
      }
    } else if (row_token == ebus::detail::JsonReader::Token::object_start) {
      row_reader.reset();
      eval_error = Command::evaluate(row_reader);
      if (!eval_error.empty()) break;
    }
  }
  return eval_error;
}

}  // namespace

CommandsApi* CommandsApi::instance_ = nullptr;

CommandsApi::CommandsApi(CommandManager& command_manager, MqttHA& mqtt_ha)
    : command_manager_(command_manager), mqtt_ha_(mqtt_ha) {
  instance_ = this;
}

bool CommandsApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  registerUri("/commands", HTTP_GET, handleCommandsPage);
  registerUri("/api/v1/app/commands", HTTP_GET, handleCommands);
  registerUri("/api/v1/app/commands/evaluate", HTTP_POST,
              handleCommandsEvaluate);
  registerUri("/api/v1/app/commands/insert", HTTP_POST, handleCommandsInsert);
  registerUri("/api/v1/app/commands/upload", HTTP_POST, handleCommandsUpload);
  registerUri("/api/v1/app/commands/remove", HTTP_POST, handleCommandsRemove);
  registerUri("/api/v1/app/commands/load", HTTP_POST, handleCommandsLoad);
  registerUri("/api/v1/app/commands/save", HTTP_POST, handleCommandsSave);
  registerUri("/api/v1/app/commands/wipe", HTTP_POST, handleCommandsWipe);

  return true;
}

esp_err_t CommandsApi::handleCommandsPage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", commands_html_start);
  return ESP_OK;
}

esp_err_t CommandsApi::handleCommands(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  instance_->command_manager_.fetchCommands([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t CommandsApi::handleCommandsEvaluate(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (req->content_len > static_cast<int>(HttpUtils::max_request_body_size)) {
    HttpUtils::sendErrorResponse(req, "413 Payload Too Large", "evaluate",
                                 "Request body too large");
    return ESP_OK;
  }
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate",
                                 "Invalid request body");
    return ESP_OK;
  }
  sr.endOfInput();

  ebus::detail::JsonReader& reader = sr.jsonReader();
  std::string parse_error;
  if (!HttpUtils::prepareJsonReaderForArray(reader, "commands", parse_error)) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate",
                                 parse_error);
    return ESP_OK;
  }

  std::string_view eval_error = evaluateCommands(reader);
  if (!eval_error.empty())
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate",
                                 eval_error);
  else
    HttpUtils::sendSuccessResponse(req, "evaluate");

  return ESP_OK;
}

esp_err_t CommandsApi::handleCommandsInsert(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (req->content_len > static_cast<int>(HttpUtils::max_request_body_size)) {
    HttpUtils::sendErrorResponse(req, "413 Payload Too Large", "insert",
                                 "Request body too large");
    return ESP_OK;
  }
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "insert",
                                 "Invalid request body");
    return ESP_OK;
  }
  sr.endOfInput();

  std::string_view body_sv = sr.jsonReader().remaining();

  ebus::detail::JsonReader reader_eval(body_sv);
  std::string parse_error;
  if (!HttpUtils::prepareJsonReaderForArray(reader_eval, "commands",
                                            parse_error)) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "insert", parse_error);
    return ESP_OK;
  }

  std::string_view eval_error = evaluateCommands(reader_eval);
  if (!eval_error.empty()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "insert", eval_error);
    return ESP_OK;
  }

  ebus::detail::JsonReader reader_insert(body_sv);
  HttpUtils::prepareJsonReaderForArray(reader_insert, "commands", parse_error);
  bool header_seen = false;
  while (true) {
    std::string_view cmd_sv = reader_insert.rawValue();
    if (cmd_sv.empty()) break;
    ebus::detail::JsonReader row_reader(cmd_sv);
    auto row_token = row_reader.next();
    if (row_token == ebus::detail::JsonReader::Token::array_start) {
      if (!header_seen) {
        header_seen = true;
        continue;
      }
      row_reader.reset();
      instance_->command_manager_.insertCommand(
          Command::fromTabular(row_reader));
    } else if (row_token == ebus::detail::JsonReader::Token::object_start) {
      row_reader.reset();
      instance_->command_manager_.insertCommand(Command::fromJson(row_reader));
    }
  }
  Mqtt::publishComponentDiscovery();
  HttpUtils::sendSuccessResponse(req, "insert");
  return ESP_OK;
}

esp_err_t CommandsApi::handleCommandsUpload(httpd_req_t* req) {
  if (req->method != HTTP_POST) {
    HttpUtils::sendErrorResponse(req, "405 Method Not Allowed", "upload",
                                 "POST required");
    return ESP_OK;
  }

  if (!instance_->command_manager_.initFileSystem()) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "LittleFS init failed");
    return ESP_OK;
  }

  const char* tmp_path = "/littlefs/commands.json.tmp";
#ifdef EBUS_COMMANDS_TMP_FILE_PATH
  char host_tmp_path[256];
  std::snprintf(host_tmp_path, sizeof(host_tmp_path), "%s.%ld",
                EBUS_COMMANDS_TMP_FILE_PATH, static_cast<long>(getpid()));
  tmp_path = host_tmp_path;
#endif

  FILE* file = std::fopen(tmp_path, "wb");
  if (file == nullptr) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "Failed to open temp file");
    return ESP_OK;
  }

  char buffer[512];
  int remaining = req->content_len;
  int total_written = 0;

  // Lossy links stall mid-transfer: tolerate receive gaps up to
  // upload_stall_budget_ms after the last byte instead of aborting on the
  // first socket timeout. Fatal socket errors still abort immediately.
  constexpr uint32_t upload_stall_budget_ms = 120000;
  uint32_t last_progress_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

  while (remaining > 0) {
    int to_read = remaining > static_cast<int>(sizeof(buffer))
                      ? static_cast<int>(sizeof(buffer))
                      : remaining;
    int received = httpd_req_recv(req, buffer, to_read);
    if (received <= 0) {
      const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
      if ((received == 0 || errno == EAGAIN || errno == EWOULDBLOCK ||
           errno == EINTR) &&
          now_ms - last_progress_ms < upload_stall_budget_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      std::fclose(file);
      std::remove(tmp_path);
      HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                   "Receive failed");
      return ESP_OK;
    }
    last_progress_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    int written = std::fwrite(buffer, 1, received, file);
    if (written != received) {
      std::fclose(file);
      std::remove(tmp_path);
      HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                   "Write failed");
      return ESP_OK;
    }
    total_written += written;
    remaining -= received;
  }

  std::fclose(file);

  int64_t bytes = instance_->command_manager_.loadCommandsFrom(tmp_path);
  if (bytes < 0) {
    std::remove(tmp_path);
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "JSON parse failed");
    return ESP_OK;
  }

  if (instance_->command_manager_.saveCommands() < 0) {
    std::remove(tmp_path);
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "Save failed");
    return ESP_OK;
  }

  std::remove(tmp_path);
  Mqtt::publishComponentDiscovery();
  size_t count = instance_->command_manager_.getCommandCount();
  char res_buf[128];
  snprintf(res_buf, sizeof(res_buf), "Uploaded %d bytes, loaded %u commands",
           total_written, (unsigned)count);
  HttpUtils::sendSuccessResponse(req, "upload", res_buf);
  return ESP_OK;
}

esp_err_t CommandsApi::handleCommandsRemove(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "remove",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();

  ebus::detail::JsonReader reader(sr.jsonReader().remaining());
  std::string parse_error;
  if (HttpUtils::prepareJsonReaderForArray(reader, "keys", parse_error)) {
    while (true) {
      auto t = reader.next();
      if (t == ebus::detail::JsonReader::Token::array_end ||
          t == ebus::detail::JsonReader::Token::end)
        break;
      if (t == ebus::detail::JsonReader::Token::string)
        instance_->command_manager_.removeCommand(reader.value());
    }
  } else {
    instance_->command_manager_.removeAll();
  }
  HttpUtils::sendSuccessResponse(req, "remove");
  return ESP_OK;
}

esp_err_t CommandsApi::handleCommandsLoad(httpd_req_t* req) {
  int64_t bytes = instance_->command_manager_.loadCommands();
  if (bytes > 0) {
    Mqtt::publishComponentDiscovery();
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

esp_err_t CommandsApi::handleCommandsSave(httpd_req_t* req) {
  int64_t bytes = instance_->command_manager_.saveCommands();
  if (bytes > 0) {
    Mqtt::publishComponentDiscovery();
    HttpUtils::sendSuccessResponse(req, "save", "successful",
                                   "Saved " + std::to_string(bytes) + " bytes");
  } else if (bytes < 0) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "save",
                                 "Save failed");
  } else {
    HttpUtils::sendSuccessResponse(req, "save", "no data");
  }
  return ESP_OK;
}

esp_err_t CommandsApi::handleCommandsWipe(httpd_req_t* req) {
  if (instance_->mqtt_ha_.isEnabled()) {
    instance_->mqtt_ha_.removeComponents();
  }
  int64_t bytes = instance_->command_manager_.wipeCommands();
  if (bytes > 0) {
    HttpUtils::sendSuccessResponse(req, "wipe", "successful",
                                   "Wiped " + std::to_string(bytes) + " bytes");
  } else if (bytes < 0) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "wipe",
                                 "Wipe failed");
  } else {
    HttpUtils::sendSuccessResponse(req, "wipe", "no data");
  }
  return ESP_OK;
}

#endif