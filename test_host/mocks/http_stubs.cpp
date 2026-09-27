// Host link stubs for HTTP server plumbing (never executed by Tier-1 tests).
// config_manager.cpp references these symbols; the real implementations need
// a live httpd server.

#include <ebus/detail/json_writer.hpp>
#include <string>

#include "network/http.hpp"
#include "network/http_utils.hpp"

bool RegisterUri(const char* uri, httpd_method_t method,
                 esp_err_t (*handler)(httpd_req_t*)) {
  (void)uri;
  (void)method;
  (void)handler;
  return true;
}

void SetupHttpHandlers() {}
void SetupHttpFallbackHandlers() {}

namespace HttpUtils {

void sendResponse(httpd_req_t* req, const char* status, const char* type,
                  const char* body) {
  if (req != nullptr) {
    req->status = status != nullptr ? status : "200 OK";
    req->content_type = type != nullptr ? type : "text/plain";
    if (body != nullptr) req->final_body.assign(body);
  }
}

void sendResponse(httpd_req_t* req, const char* status, const char* type,
                  const std::string& body) {
  sendResponse(req, status, type, body.c_str());
}

void applyCustomHeaders(httpd_req_t* req) { (void)req; }

void sendErrorResponse(httpd_req_t* req, const char* status,
                       std::string_view id, std::string_view message) {
  if (req == nullptr) return;
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  std::string body;
  ebus::detail::JsonWriter writer(
      [&body](std::string_view chunk) { body.append(chunk); });
  {
    auto scope = writer.objectScope();
    writer.writeField("id", id);
    writer.writeField("status", "failed");
    writer.writeField("error", message);
  }
  httpd_resp_send(req, body.data(), body.size());
}

void sendSuccessResponse(httpd_req_t* req, std::string_view id,
                         std::string_view result, std::string_view message) {
  if (req == nullptr) return;
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  std::string body;
  ebus::detail::JsonWriter writer(
      [&body](std::string_view chunk) { body.append(chunk); });
  {
    auto scope = writer.objectScope();
    writer.writeField("id", id);
    writer.writeField("status", result);
    if (!message.empty()) writer.writeField("message", message);
  }
  httpd_resp_send(req, body.data(), body.size());
}

void setCustomHeaders(const std::string& raw) { (void)raw; }

StreamingReader::StreamingReader(httpd_req_t* req) : req_(req) {
  if (req_ == nullptr) return;
  if (req_->content_len > 0) {
    fallback_body_ = req_->body;
    fallback_reader_ = ebus::detail::JsonReader(fallback_body_);
    valid_ = !fallback_body_.empty();
  } else {
    valid_ = true;
  }
}

StreamingReader::~StreamingReader() = default;

bool StreamingReader::feedAll() {
  if (req_ == nullptr) return false;
  if (req_->content_len <= 0) return true;
  fallback_body_ = req_->body;
  fallback_reader_ = ebus::detail::JsonReader(fallback_body_);
  valid_ = !fallback_body_.empty();
  return valid_;
}

void StreamingReader::endOfInput() const {}

ebus::detail::JsonReader& StreamingReader::jsonReader() {
  return fallback_reader_;
}

}  // namespace HttpUtils
