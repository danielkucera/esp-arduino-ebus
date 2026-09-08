#pragma once

#if defined(EBUS_INTERNAL)

#include <esp_http_server.h>

#include "logger.hpp"

class LogsApi {
 public:
  explicit LogsApi(Logger& logger);

  bool registerHandlers(httpd_handle_t server);

 private:
  Logger& logger_;

  static LogsApi* instance_;

  static esp_err_t handleLogsPage(httpd_req_t* req);
  static esp_err_t handleLogs(httpd_req_t* req);
  static esp_err_t handleLogsTimeRelation(httpd_req_t* req);
};

#endif
