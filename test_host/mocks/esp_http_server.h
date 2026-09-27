#pragma once

// Host mock for ESP-IDF esp_http_server.h: types + no-op responses.
// Only what app TU declarations need to compile and link on host.

#include <sys/types.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

constexpr int HTTPD_414_URI_TOO_LONG = 414;

typedef enum {
  HTTP_GET = 0,
  HTTP_PUT,
  HTTP_POST,
  HTTP_DELETE,
  HTTP_HEAD,
  HTTP_OPTIONS,
  HTTP_PATCH,
} httpd_method_t;

typedef struct httpd_req {
  std::string uri;
  std::string query;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string status;
  std::string content_type;
  std::vector<std::string> chunks;
  std::string final_body;
  int content_len = 0;
  httpd_method_t method = HTTP_GET;
  bool has_query = false;
} httpd_req_t;
typedef struct httpd_data* httpd_handle_t;
typedef struct httpd_uri httpd_uri_t;

inline esp_err_t httpd_resp_set_status(httpd_req_t* req, const char* status) {
  if (req == nullptr || status == nullptr) return ESP_OK;
  req->status = status;
  return ESP_OK;
}

inline esp_err_t httpd_resp_set_type(httpd_req_t* req, const char* type) {
  if (req == nullptr || type == nullptr) return ESP_OK;
  req->content_type = type;
  return ESP_OK;
}

inline esp_err_t httpd_resp_set_hdr(httpd_req_t* req, const char* name,
                                    const char* value) {
  if (req == nullptr || name == nullptr || value == nullptr) return ESP_OK;
  req->headers.emplace_back(name, value);
  return ESP_OK;
}

inline esp_err_t httpd_resp_send(httpd_req_t* req, const char* buf,
                                 size_t buf_len) {
  if (req == nullptr) return ESP_OK;
  if (buf != nullptr && buf_len > 0) {
    req->final_body.assign(buf, buf_len);
  } else {
    req->final_body.clear();
  }
  return ESP_OK;
}

inline esp_err_t httpd_resp_send_chunk(httpd_req_t* req, const char* buf,
                                       ssize_t buf_len) {
  if (req == nullptr) return ESP_OK;
  if (buf == nullptr || buf_len == 0) {
    req->chunks.emplace_back();
    return ESP_OK;
  }
  req->chunks.emplace_back(buf, static_cast<size_t>(buf_len));
  return ESP_OK;
}

inline esp_err_t httpd_resp_send_err(httpd_req_t* req, int code,
                                     const char* message) {
  (void)message;
  if (req != nullptr) {
    req->status = code == HTTPD_414_URI_TOO_LONG ? "414 URI Too Long" : "error";
  }
  return ESP_OK;
}

inline size_t httpd_req_get_url_query_len(httpd_req_t* req) {
  if (req == nullptr || req->query.empty()) return 0;
  return req->query.size();
}

inline esp_err_t httpd_req_get_url_query_str(httpd_req_t* req, char* buf,
                                             size_t buf_len) {
  if (req == nullptr || buf == nullptr || buf_len == 0) return ESP_FAIL;
  if (req->query.empty()) return ESP_FAIL;
  const size_t copied = std::min(req->query.size(), buf_len - 1);
  std::memcpy(buf, req->query.c_str(), copied);
  buf[copied] = '\0';
  return ESP_OK;
}

inline esp_err_t httpd_query_key_value(const char* query, const char* key,
                                       char* value, size_t value_size) {
  if (query == nullptr || key == nullptr || value == nullptr || value_size == 0)
    return ESP_FAIL;
  const std::string q(query);
  const std::string pattern = std::string(key) + "=";
  const size_t pos = q.find(pattern);
  if (pos == std::string::npos) return ESP_FAIL;
  const size_t start = pos + pattern.size();
  const size_t end = q.find('&', start);
  const size_t len = end == std::string::npos ? q.size() - start : end - start;
  const size_t copy = std::min(len, value_size - 1);
  std::memcpy(value, q.c_str() + start, copy);
  value[copy] = '\0';
  return ESP_OK;
}

inline int httpd_req_recv(httpd_req_t* req, char* buf, size_t len) {
  if (req == nullptr || buf == nullptr || len == 0) return 0;
  if (req->body.empty()) return 0;
  const size_t to_copy = std::min(req->body.size(), len);
  std::memcpy(buf, req->body.data(), to_copy);
  req->body.erase(0, to_copy);
  return static_cast<int>(to_copy);
}

#ifdef __cplusplus
}
#endif
