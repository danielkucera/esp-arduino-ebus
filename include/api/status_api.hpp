#pragma once

#include <esp_http_server.h>

class StatusApi {
 public:
  explicit StatusApi();

  static bool registerHandlers(httpd_handle_t server);

 private:
  static esp_err_t handleStatusPage(httpd_req_t* req);
#if defined(EBUS_INTERNAL)
  static esp_err_t handleHealth(httpd_req_t* req);
#endif
};
