#pragma once

#include <esp_http_server.h>

class StatusApi {
 public:
  explicit StatusApi();

  bool registerHandlers(httpd_handle_t server);

 private:
  static StatusApi* instance_;

  static esp_err_t handleStatusPage(httpd_req_t* req);
  static esp_err_t handleStatus(httpd_req_t* req);

#if defined(EBUS_INTERNAL)
  static esp_err_t handleStatusApp(httpd_req_t* req);
  static esp_err_t handleStatusLib(httpd_req_t* req);
#endif
};
