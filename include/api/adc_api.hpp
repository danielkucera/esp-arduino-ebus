#pragma once

#include <esp_http_server.h>

#include "adc.hpp"

class AdcApi {
 public:
  explicit AdcApi(Adc& adc);

  bool registerHandlers(httpd_handle_t server);

 private:
  Adc& adc_;

  static AdcApi* instance_;

  static esp_err_t handleAdcPage(httpd_req_t* req);
  static esp_err_t handleAdcRaw(httpd_req_t* req);
  static esp_err_t handleAdcState(httpd_req_t* req);
  static esp_err_t handleAdcEnable(httpd_req_t* req);
  static esp_err_t handleAdcDisable(httpd_req_t* req);
};
