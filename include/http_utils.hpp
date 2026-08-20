#pragma once

#include <esp_http_server.h>

#include <ebus/detail/json_reader.hpp>
#include <string>
#include <string_view>

namespace HttpUtils {

constexpr size_t max_request_body_size = 8192;
constexpr size_t streaming_buffer_size = 4096;

class StreamingReader {
 public:
  explicit StreamingReader(httpd_req_t* req);
  ~StreamingReader();
  StreamingReader(const StreamingReader&) = delete;
  StreamingReader& operator=(const StreamingReader&) = delete;

  bool feedAll();
  void endOfInput() const;
  ebus::detail::JsonReader& jsonReader();
  bool isValid() const { return valid_; }

 private:
  httpd_req_t* req_;
  std::string fallback_body_;
  ebus::detail::JsonReader fallback_reader_;
  bool valid_ = false;
  bool use_streaming_ = false;
};

bool registerRoute(httpd_handle_t server, const httpd_uri_t& route);
bool registerRoute(httpd_handle_t server, const char* uri,
                   httpd_method_t method, esp_err_t (*handler)(httpd_req_t*));

void sendResponse(httpd_req_t* req, const char* status, const char* type,
                  const char* body);
void sendResponse(httpd_req_t* req, const char* status, const char* type,
                  const std::string& body);

std::string readBody(httpd_req_t* req);

// Parse and store custom headers (format: "Name: Value" lines,
// newline-separated). Must be called once at startup; stored headers are
// applied to every response.
void setCustomHeaders(const std::string& raw);

// Sends a standardized JSON error response.
void sendErrorResponse(httpd_req_t* req, const char* status,
                       std::string_view id, std::string_view error_message);

// Sends a standardized JSON success response.
void sendSuccessResponse(httpd_req_t* req, std::string_view id,
                         std::string_view status = "successful",
                         std::string_view message = "");

// Applies the currently stored custom headers to the given HTTP response.
// Useful for handlers that use chunked/streaming responses.
void applyCustomHeaders(httpd_req_t* req);

}  // namespace HttpUtils
