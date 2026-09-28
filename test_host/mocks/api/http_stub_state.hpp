#pragma once

#include <string>
#include <vector>

#include "esp_http_server.h"

namespace HostHttpStub {

struct Route {
  std::string uri;
  httpd_method_t method;
  esp_err_t (*handler)(httpd_req_t*);
};

void clearRoutes();
void addRoute(const char* uri, httpd_method_t method,
              esp_err_t (*handler)(httpd_req_t*));
const std::vector<Route>& routes();

}  // namespace HostHttpStub
