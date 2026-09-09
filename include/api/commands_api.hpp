#pragma once

#if defined(EBUS_INTERNAL)

#include <esp_http_server.h>

#include "command_manager.hpp"

class CommandsApi {
 public:
  explicit CommandsApi(CommandManager& command_manager);

  bool registerHandlers(httpd_handle_t server);

 private:
  CommandManager& command_manager_;

  static CommandsApi* instance_;

  static esp_err_t handleCommandsPage(httpd_req_t* req);
  static esp_err_t handleCommands(httpd_req_t* req);
  static esp_err_t handleCommandsEvaluate(httpd_req_t* req);
  static esp_err_t handleCommandsInsert(httpd_req_t* req);
  static esp_err_t handleCommandsUpload(httpd_req_t* req);
  static esp_err_t handleCommandsRemove(httpd_req_t* req);
  static esp_err_t handleCommandsLoad(httpd_req_t* req);
  static esp_err_t handleCommandsSave(httpd_req_t* req);
  static esp_err_t handleCommandsWipe(httpd_req_t* req);
};

#endif
