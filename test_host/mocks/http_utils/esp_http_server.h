#pragma once

#include_next <esp_http_server.h>

struct httpd_uri {
  const char* uri = nullptr;
  httpd_method_t method = HTTP_GET;
  esp_err_t (*handler)(httpd_req_t*) = nullptr;
  void* user_ctx = nullptr;
};

namespace HttpUtilsTestState {
inline esp_err_t route_result = ESP_OK;
}

inline esp_err_t httpd_register_uri_handler(httpd_handle_t,
                                            const httpd_uri_t*) {
  return HttpUtilsTestState::route_result;
}
