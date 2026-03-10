#include "HttpUtils.hpp"

#include <esp_err.h>

namespace HttpUtils {

void sendResponse(httpd_req_t* req, const char* status, const char* type,
                  const String& body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, type);
  httpd_resp_send(req, body.c_str(), body.length());
}

String readBody(httpd_req_t* req) {
  String out;
  int remaining = req->content_len;
  char buffer[512];

  while (remaining > 0) {
    int toRead = remaining > static_cast<int>(sizeof(buffer))
                     ? sizeof(buffer)
                     : remaining;
    int received = httpd_req_recv(req, buffer, toRead);
    if (received <= 0) return "";
    out.concat(buffer, received);
    remaining -= received;
  }

  return out;
}

bool registerRoute(httpd_handle_t server, const httpd_uri_t& route) {
  const esp_err_t err = httpd_register_uri_handler(server, &route);
  if (err != ESP_OK) {
    log_e("HTTP route register failed: %s (%s)", route.uri, esp_err_to_name(err));
    return false;
  }
  return true;
}

bool registerRoute(httpd_handle_t server, const char* uri, httpd_method_t method,
                   esp_err_t (*handler)(httpd_req_t*)) {
  httpd_uri_t route = {};
  route.uri = uri;
  route.method = method;
  route.handler = handler;
  return registerRoute(server, route);
}

}  // namespace HttpUtils
