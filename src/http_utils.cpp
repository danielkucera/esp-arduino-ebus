#include "http_utils.hpp"

#include <esp_err.h>

#include <cstring>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>
#include <mutex>
#include <utility>

#include "logger.hpp"

namespace HttpUtils {

namespace {
std::vector<std::pair<std::string, std::string>> customHeaders;

char streaming_buffer[streaming_buffer_size];
ebus::detail::JsonReader streaming_reader(streaming_buffer,
                                          sizeof(streaming_buffer));
std::mutex streaming_mutex;
}  // namespace

StreamingReader::StreamingReader(httpd_req_t* req) : req_(req) {
  if (req_->content_len == 0) {
    valid_ = true;
    use_streaming_ = true;
    streaming_mutex.lock();
    streaming_reader.reset();
    return;
  }
  if (req_->content_len > static_cast<int>(max_request_body_size)) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "HTTP: Request body too large (%d bytes), max is %zu",
             req_->content_len, max_request_body_size);
    logger.warn(buf);
    return;
  }
  if (req_->content_len <= static_cast<int>(streaming_buffer_size)) {
    valid_ = true;
    use_streaming_ = true;
    streaming_mutex.lock();
    streaming_reader.reset();
  } else {
    fallback_body_ = readBody(req_);
    if (fallback_body_.empty() && req_->content_len > 0) {
      return;
    }
    valid_ = true;
    fallback_reader_ = ebus::detail::JsonReader(fallback_body_);
  }
}

StreamingReader::~StreamingReader() {
  if (use_streaming_) {
    streaming_mutex.unlock();
  }
}

bool StreamingReader::feedAll() {
  if (!use_streaming_) return true;
  char tmp[512];
  int remaining = req_->content_len;
  while (remaining > 0) {
    int toRead = std::min(remaining, static_cast<int>(sizeof(tmp)));
    int received = httpd_req_recv(req_, tmp, toRead);
    if (received <= 0) {
      return false;
    }
    streaming_reader.feed(std::string_view(tmp, static_cast<size_t>(received)));
    remaining -= received;
  }
  return true;
}

void StreamingReader::endOfInput() const {
  if (use_streaming_) {
    streaming_reader.endOfInput();
  }
}

ebus::detail::JsonReader& StreamingReader::jsonReader() {
  if (use_streaming_) {
    return streaming_reader;
  }
  return fallback_reader_;
}

bool registerRoute(httpd_handle_t server, const httpd_uri_t& route) {
  const esp_err_t err = httpd_register_uri_handler(server, &route);
  if (err != ESP_OK) {
    char buf[128];
    snprintf(buf, sizeof(buf), "HTTP route register failed: %s (%s)", route.uri,
             esp_err_to_name(err));
    logger.error(buf);
    return false;
  }
  return true;
}

bool registerRoute(httpd_handle_t server, const char* uri,
                   httpd_method_t method, esp_err_t (*handler)(httpd_req_t*)) {
  httpd_uri_t route = {};
  route.uri = uri;
  route.method = method;
  route.handler = handler;
  return registerRoute(server, route);
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
  int remaining = req->content_len;
  if (remaining > static_cast<int>(max_request_body_size)) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "HTTP: Request body too large (%d bytes), max is %zu", remaining,
             max_request_body_size);
    logger.warn(buf);
    return "";
  }

  // Pre-allocate string to avoid repeated reallocations
  std::string out;
  out.reserve(remaining);

  char tmp[512];
  while (remaining > 0) {
    int toRead =
        remaining > static_cast<int>(sizeof(tmp)) ? sizeof(tmp) : remaining;
    int received = httpd_req_recv(req, tmp, toRead);
    if (received <= 0) return "";
    out.append(tmp, received);
    remaining -= received;
  }

  return out;
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

void sendErrorResponse(httpd_req_t* req, const char* status,
                       std::string_view id, std::string_view error_message) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  applyCustomHeaders(req);
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  {
    auto scope = writer.objectScope();
    writer.writeField("id", id);
    writer.writeField("status", "failed");
    writer.writeField("error", error_message);
  }
  httpd_resp_send(req, out.c_str(), out.length());
}

void sendSuccessResponse(httpd_req_t* req, std::string_view id,
                         std::string_view status, std::string_view message) {
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  applyCustomHeaders(req);
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  {
    auto scope = writer.objectScope();
    writer.writeField("id", id);
    writer.writeField("status", status);
    if (!message.empty()) writer.writeField("message", message);
  }
  httpd_resp_send(req, out.c_str(), out.length());
}

void applyCustomHeaders(httpd_req_t* req) {
  for (const auto& h : customHeaders)
    httpd_resp_set_hdr(req, h.first.c_str(), h.second.c_str());
}

}  // namespace HttpUtils
