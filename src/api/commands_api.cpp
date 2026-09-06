#include "api/commands_api.hpp"

#if defined(EBUS_INTERNAL)

#include <ebus/detail/json_reader.hpp>

#include "command_manager.hpp"
#include "http.hpp"
#include "http_utils.hpp"
#include "logger.hpp"
#include "mqtt.hpp"
#include "mqtt_ha.hpp"

namespace {

// cppcheck-suppress syntaxError
extern const char commands_html_start[] asm("_binary_commands_html_start");

}  // namespace

CommandsApi* CommandsApi::instance_ = nullptr;

CommandsApi::CommandsApi(CommandManager& command_manager)
    : command_manager_(command_manager) {
  instance_ = this;
}

bool CommandsApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  RegisterUri("/commands", HTTP_GET, handleCommandsPage);
  RegisterUri("/api/v1/commands", HTTP_GET, handleCommands);
  RegisterUri("/api/v1/commands/evaluate", HTTP_POST, handleCommandsEvaluate);
  RegisterUri("/api/v1/commands/insert", HTTP_POST, handleCommandsInsert);
  RegisterUri("/api/v1/commands/upload", HTTP_POST, handleCommandsUpload);
  RegisterUri("/api/v1/commands/remove", HTTP_POST, handleCommandsRemove);
  RegisterUri("/api/v1/commands/load", HTTP_POST, handleCommandsLoad);
  RegisterUri("/api/v1/commands/save", HTTP_POST, handleCommandsSave);
  RegisterUri("/api/v1/commands/wipe", HTTP_POST, handleCommandsWipe);

  return true;
}

esp_err_t CommandsApi::handleCommandsPage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", commands_html_start);
  return ESP_OK;
}

esp_err_t CommandsApi::handleCommands(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  commandManager.fetchCommands([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t CommandsApi::handleCommandsEvaluate(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate",
                                 "Request body too large or invalid");
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

  std::string_view evalError;
  bool headerSeen = false;
  while (true) {
    std::string_view row_sv = reader.rawValue();
    if (row_sv.empty()) break;
    ebus::detail::JsonReader row_reader(row_sv);
    auto row_token = row_reader.next();

    if (row_token == ebus::detail::JsonReader::Token::array_start) {
      if (!headerSeen) {
        headerSeen = true;
        continue;
      }
    } else if (row_token == ebus::detail::JsonReader::Token::object_start) {
      row_reader.reset();
      evalError = Command::evaluate(row_reader);
      if (!evalError.empty()) break;
    }
  }

  if (!evalError.empty())
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate", evalError);
  else
    HttpUtils::sendSuccessResponse(req, "evaluate");

  return ESP_OK;
}

esp_err_t CommandsApi::handleCommandsInsert(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "insert",
                                 "Request body too large or invalid");
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

  std::string_view evalError;
  bool headerSeen = false;
  while (true) {
    std::string_view cmd_sv = reader_eval.rawValue();
    if (cmd_sv.empty()) break;
    ebus::detail::JsonReader row_reader(cmd_sv);
    auto row_token = row_reader.next();
    if (row_token == ebus::detail::JsonReader::Token::array_start) {
      if (!headerSeen) {
        headerSeen = true;
        continue;
      }
    } else if (row_token == ebus::detail::JsonReader::Token::object_start) {
      row_reader.reset();
      evalError = Command::evaluate(row_reader);
      if (!evalError.empty()) break;
    }
  }

  if (!evalError.empty()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "insert", evalError);
    return ESP_OK;
  }

  ebus::detail::JsonReader reader_insert(body_sv);
  HttpUtils::prepareJsonReaderForArray(reader_insert, "commands", parse_error);
  headerSeen = false;
  while (true) {
    std::string_view cmd_sv = reader_insert.rawValue();
    if (cmd_sv.empty()) break;
    ebus::detail::JsonReader row_reader(cmd_sv);
    auto row_token = row_reader.next();
    if (row_token == ebus::detail::JsonReader::Token::array_start) {
      if (!headerSeen) {
        headerSeen = true;
        continue;
      }
      row_reader.reset();
      commandManager.insertCommand(Command::fromTabular(row_reader));
    } else if (row_token == ebus::detail::JsonReader::Token::object_start) {
      row_reader.reset();
      commandManager.insertCommand(Command::fromJson(row_reader));
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

  if (!commandManager.initFileSystem()) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "LittleFS init failed");
    return ESP_OK;
  }

  const char* tmp_path = "/littlefs/commands.json.tmp";

  FILE* file = std::fopen(tmp_path, "wb");
  if (file == nullptr) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "Failed to open temp file");
    return ESP_OK;
  }

  char buffer[512];
  int remaining = req->content_len;
  int total_written = 0;

  while (remaining > 0) {
    int to_read = remaining > static_cast<int>(sizeof(buffer))
                      ? static_cast<int>(sizeof(buffer))
                      : remaining;
    int received = httpd_req_recv(req, buffer, to_read);
    if (received <= 0) {
      std::fclose(file);
      std::remove(tmp_path);
      HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                   "Receive failed");
      return ESP_OK;
    }
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

  int64_t bytes = commandManager.loadCommandsFrom(tmp_path);
  if (bytes < 0) {
    std::remove(tmp_path);
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "JSON parse failed");
    return ESP_OK;
  }

  if (commandManager.saveCommands() < 0) {
    std::remove(tmp_path);
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "Save failed");
    return ESP_OK;
  }

  std::remove(tmp_path);
  Mqtt::publishComponentDiscovery();
  size_t count = commandManager.getCommandCount();
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
        commandManager.removeCommand(reader.value());
    }
  } else {
    commandManager.removeAll();
  }
  HttpUtils::sendSuccessResponse(req, "remove");
  return ESP_OK;
}

esp_err_t CommandsApi::handleCommandsLoad(httpd_req_t* req) {
  int64_t bytes = commandManager.loadCommands();
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
  int64_t bytes = commandManager.saveCommands();
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
  if (mqttha.isEnabled()) {
    mqttha.removeComponents();
  }
  int64_t bytes = commandManager.wipeCommands();
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