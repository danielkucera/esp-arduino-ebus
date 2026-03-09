#pragma once

#include <Arduino.h>
#include <esp_http_server.h>

namespace HttpUtils {

void sendResponse(httpd_req_t* req, const char* status, const char* type,
                  const String& body);

String readBody(httpd_req_t* req);

bool registerRoute(httpd_handle_t server, const httpd_uri_t& route);

bool registerRoute(httpd_handle_t server, const char* uri, httpd_method_t method,
                   esp_err_t (*handler)(httpd_req_t*), void* user_ctx = nullptr);

}  // namespace HttpUtils

