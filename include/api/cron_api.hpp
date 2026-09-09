#pragma once

#if defined(EBUS_INTERNAL)

#include <esp_http_server.h>

#include "cron.hpp"

class CronApi {
 public:
  explicit CronApi(Cron& cron);

  bool registerHandlers(httpd_handle_t server);

 private:
  Cron& cron_;

  static CronApi* instance_;

  static esp_err_t handleCronPage(httpd_req_t* req);
  static esp_err_t handleCron(httpd_req_t* req);
  static esp_err_t handleCronEvaluate(httpd_req_t* req);
  static esp_err_t handleCronSave(httpd_req_t* req);
  static esp_err_t handleCronLoad(httpd_req_t* req);
};

#endif