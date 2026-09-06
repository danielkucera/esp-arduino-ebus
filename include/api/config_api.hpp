#pragma once

#include <esp_http_server.h>

class ConfigApi {
 public:
  explicit ConfigApi();

  bool registerHandlers(httpd_handle_t server);

 private:
  static ConfigApi* instance_;

  static esp_err_t handleConfigPage(httpd_req_t* req);
  static esp_err_t handleWifiScan(httpd_req_t* req);
};
