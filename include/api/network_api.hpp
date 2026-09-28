#pragma once

#if defined(EBUS_INTERNAL)

#include <esp_http_server.h>

class NetworkApi {
 public:
  explicit NetworkApi();

  static bool registerHandlers(httpd_handle_t server);

 private:
  static esp_err_t handleNetwork(httpd_req_t* req);
  static esp_err_t handleWifiScan(httpd_req_t* req);
};

#endif
