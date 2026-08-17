#pragma once

#include <esp_http_server.h>

#include <string>

namespace HttpUtils {

// Maximum allowed size for request bodies to prevent memory exhaustion.
constexpr size_t kMaxRequestBodySize = 8192;  // 8KB

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
