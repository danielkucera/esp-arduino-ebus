#pragma once

#if defined(EBUS_INTERNAL)

#include <esp_http_server.h>

#include "command_manager.hpp"

class ValuesApi {
 public:
  explicit ValuesApi(CommandManager& command_manager);

  bool registerHandlers(httpd_handle_t server);

 private:
  CommandManager& command_manager_;

  static ValuesApi* instance_;

  static esp_err_t handleValuesPage(httpd_req_t* req);
  static esp_err_t handleValues(httpd_req_t* req);
  static esp_err_t handleValuesWrite(httpd_req_t* req);
  static esp_err_t handleValuesRead(httpd_req_t* req);
};

#endif