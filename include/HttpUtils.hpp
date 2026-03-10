#pragma once

#include <esp_http_server.h>

#include <string>

namespace HttpUtils {

void sendResponse(httpd_req_t* req, const char* status, const char* type,
                  const std::string& body);
void sendResponse(httpd_req_t* req, const char* status, const char* type,
                  const char* body);

std::string readBody(httpd_req_t* req);

bool registerRoute(httpd_handle_t server, const httpd_uri_t& route);

bool registerRoute(httpd_handle_t server, const char* uri, httpd_method_t method,
                   esp_err_t (*handler)(httpd_req_t*));

}  // namespace HttpUtils
