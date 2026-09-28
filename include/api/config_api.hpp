#pragma once

#include <esp_http_server.h>

class ConfigApi {
 public:
  explicit ConfigApi();

  static bool registerHandlers(httpd_handle_t server);

 private:
  static esp_err_t handleConfigPage(httpd_req_t* req);
};
