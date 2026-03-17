#include "HttpUtils.hpp"

#include <esp_err.h>

#include <cstring>
#include <utility>
#include <vector>

#include "Logger.hpp"

namespace HttpUtils {

namespace {
  std::vector<std::pair<std::string, std::string>> customHeaders;
}

void setCustomHeaders(const std::string& raw) {
  customHeaders.clear();
  size_t pos = 0;
  while (pos < raw.size()) {
    size_t end = raw.find('\n', pos);
    if (end == std::string::npos) end = raw.size();
    std::string line = raw.substr(pos, end - pos);
    // Strip trailing \r
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t colon = line.find(':');
    if (colon != std::string::npos && colon > 0) {
      std::string name = line.substr(0, colon);
      std::string value = line.substr(colon + 1);
      // Trim leading space from value
      if (!value.empty() && value.front() == ' ') value.erase(0, 1);
      if (!name.empty() && !value.empty())
        customHeaders.emplace_back(std::move(name), std::move(value));
    }
    pos = end + 1;
  }
}

static void applyCustomHeaders(httpd_req_t* req) {
  for (const auto& h : customHeaders)
    httpd_resp_set_hdr(req, h.first.c_str(), h.second.c_str());
}

void sendResponse(httpd_req_t* req, const char* status, const char* type,
                  const char* body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, type);
  applyCustomHeaders(req);
  const size_t len = body != nullptr ? std::strlen(body) : 0;
  httpd_resp_send(req, body != nullptr ? body : "", len);
}

void sendResponse(httpd_req_t* req, const char* status, const char* type,
                  const std::string& body) {
  sendResponse(req, status, type, body.c_str());
}

std::string readBody(httpd_req_t* req) {
  std::string out;
  int remaining = req->content_len;
  char buffer[512];

  while (remaining > 0) {
    int toRead = remaining > static_cast<int>(sizeof(buffer))
                     ? sizeof(buffer)
                     : remaining;
    int received = httpd_req_recv(req, buffer, toRead);
    if (received <= 0) return "";
    out.append(buffer, received);
    remaining -= received;
  }

  return out;
}

bool registerRoute(httpd_handle_t server, const httpd_uri_t& route) {
  const esp_err_t err = httpd_register_uri_handler(server, &route);
  if (err != ESP_OK) {
    logger.error(std::string("HTTP route register failed: ") + route.uri +
                 " (" + esp_err_to_name(err) + ")");
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
