#pragma once

#if defined(EBUS_INTERNAL)

#include <esp_http_server.h>

class MetricsApi {
 public:
  explicit MetricsApi();

  bool registerHandlers(httpd_handle_t server);

 private:
  static MetricsApi* instance_;

  static esp_err_t handleMetricsPage(httpd_req_t* req);
  static esp_err_t handleMetrics(httpd_req_t* req);
  static esp_err_t handleMetricsReset(httpd_req_t* req);
};

#endif
