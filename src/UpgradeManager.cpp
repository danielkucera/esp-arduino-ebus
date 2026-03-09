#include "UpgradeManager.hpp"

#include <cJSON.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Logger.hpp"
#include "main.hpp"

namespace {
constexpr size_t kOtaBufferSize = 1024;
constexpr uint8_t kEspImageMagic = 0xE9;
constexpr size_t kProgressStepBytes = 64 * 1024;

void registerRoute(httpd_handle_t server, const httpd_uri_t& route) {
  const esp_err_t err = httpd_register_uri_handler(server, &route);
  if (err != ESP_OK) {
    logger.error(String("HTTP route register failed: ") + route.uri + " (" +
                 esp_err_to_name(err) + ")");
  }
}

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

int findSequence(const std::vector<uint8_t>& data, const std::string& needle) {
  if (needle.empty() || data.size() < needle.size()) return -1;

  for (size_t i = 0; i + needle.size() <= data.size(); ++i) {
    if (std::equal(needle.begin(), needle.end(), data.begin() + i)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

String getContentType(httpd_req_t* req) {
  const size_t len = httpd_req_get_hdr_value_len(req, "Content-Type");
  if (len == 0 || len > 255) return "";

  char buf[256];
  if (httpd_req_get_hdr_value_str(req, "Content-Type", buf, sizeof(buf)) != ESP_OK) {
    return "";
  }
  return String(buf);
}

bool extractMultipartBoundary(const String& contentType, std::string& boundary) {
  int marker = contentType.indexOf("boundary=");
  if (marker < 0) return false;

  String raw = contentType.substring(marker + 9);
  raw.trim();
  int semicolon = raw.indexOf(';');
  if (semicolon >= 0) raw = raw.substring(0, semicolon);
  raw.trim();
  if (raw.startsWith("\"") && raw.endsWith("\"") && raw.length() >= 2) {
    raw = raw.substring(1, raw.length() - 1);
  }
  if (raw.isEmpty()) return false;

  boundary = raw.c_str();
  return !boundary.empty();
}
}  // namespace

void UpgradeManager::begin(httpd_handle_t server) {
  server_ = server;
  if (server_ == nullptr) {
    logger.error("UpgradeManager: HTTP server handle is null");
    return;
  }

  httpd_uri_t statusUri = {};
  statusUri.uri = "/api/v1/upgrade/status";
  statusUri.method = HTTP_GET;
  statusUri.handler = &UpgradeManager::handleStatusTrampoline;
  statusUri.user_ctx = this;
  registerRoute(server_, statusUri);

  httpd_uri_t httpUri = {};
  httpUri.uri = "/api/v1/upgrade/http";
  httpUri.method = HTTP_POST;
  httpUri.handler = &UpgradeManager::handleHttpUpgradeTrampoline;
  httpUri.user_ctx = this;
  registerRoute(server_, httpUri);

  httpd_uri_t uploadUri = {};
  uploadUri.uri = "/api/v1/upgrade/upload";
  uploadUri.method = HTTP_POST;
  uploadUri.handler = &UpgradeManager::handleUploadTrampoline;
  uploadUri.user_ctx = this;
  registerRoute(server_, uploadUri);
}

void UpgradeManager::setPreUpgradeHook(PreUpgradeHook hook) {
  preUpgradeHook_ = hook;
}

void UpgradeManager::prepareForUpgrade() {
  if (!preUpgradeDone_ && preUpgradeHook_) {
    preUpgradeHook_();
    preUpgradeDone_ = true;
  }
}

void UpgradeManager::resetUploadState() {
  uploadPartition_ = nullptr;
  uploadHandle_ = 0;
  preUpgradeDone_ = false;
  uploadBytesReceived_ = 0;
  uploadNextProgressPercent_ = 10;
}

esp_err_t UpgradeManager::handleUploadTrampoline(httpd_req_t* req) {
  return static_cast<UpgradeManager*>(req->user_ctx)->handleUpload(req);
}

esp_err_t UpgradeManager::handleUpload(httpd_req_t* req) {
  wdt_feed();
  resetUploadState();
  prepareForUpgrade();

  uploadPartition_ = esp_ota_get_next_update_partition(nullptr);
  if (uploadPartition_ == nullptr) {
    sendResponse(req, "500 Internal Server Error", "text/plain",
                 "No OTA partition available");
    return ESP_OK;
  }

  esp_err_t beginResult =
      esp_ota_begin(uploadPartition_, OTA_SIZE_UNKNOWN, &uploadHandle_);
  if (beginResult != ESP_OK) {
    sendResponse(req, "500 Internal Server Error", "text/plain",
                 String("esp_ota_begin failed: ") + esp_err_to_name(beginResult));
    return ESP_OK;
  }

  uint8_t buffer[kOtaBufferSize];
  bool checkedMagic = false;
  int remaining = req->content_len;
  const String contentType = getContentType(req);
  std::string multipartBoundary;
  const bool isMultipart = contentType.indexOf("multipart/form-data") >= 0 &&
                           extractMultipartBoundary(contentType, multipartBoundary);
  const std::string multipartDelimiter = "\r\n--" + multipartBoundary;
  bool multipartStarted = false;
  bool multipartFinished = false;
  std::vector<uint8_t> pending;
  int writeError = 0;  // 1=invalid_magic, 2=ota_write_failed
  size_t nextProgressBytes = kProgressStepBytes;

  logger.info("Upload started: content_len=" + String(req->content_len) +
              ", multipart=" + String(isMultipart ? 1 : 0));

  auto abortUpload = [&](const char* status, const char* message) -> esp_err_t {
    esp_ota_abort(uploadHandle_);
    sendResponse(req, status, "text/plain", message);
    return ESP_OK;
  };

  auto writeOtaChunk = [&](const uint8_t* data, size_t len) -> bool {
    if (len == 0) return true;

    if (!checkedMagic) {
      checkedMagic = true;
      if (data[0] != kEspImageMagic) {
        writeError = 1;
        return false;
      }
    }

    uploadBytesReceived_ += len;
    esp_err_t writeResult = esp_ota_write(uploadHandle_, data, len);
    wdt_feed();
    if (writeResult != ESP_OK) {
      writeError = 2;
      return false;
    }

    if (req->content_len > 0) {
      int percent = static_cast<int>((uploadBytesReceived_ * 100) / req->content_len);
      if (percent >= uploadNextProgressPercent_) {
        logger.info("Upload progress " + String(percent) + "% (" +
                    String(uploadBytesReceived_) + "/" + String(req->content_len) +
                    " bytes)");
        while (percent >= uploadNextProgressPercent_ &&
               uploadNextProgressPercent_ < 100) {
          uploadNextProgressPercent_ += 10;
        }
      }
    } else if (uploadBytesReceived_ >= nextProgressBytes) {
      logger.info("Upload progress " + String(uploadBytesReceived_) + " bytes");
      while (uploadBytesReceived_ >= nextProgressBytes) {
        nextProgressBytes += kProgressStepBytes;
      }
    }
    return true;
  };

  while (remaining > 0) {
    int toRead = remaining > static_cast<int>(sizeof(buffer))
                     ? sizeof(buffer)
                     : remaining;
    int received = httpd_req_recv(req, reinterpret_cast<char*>(buffer), toRead);
    if (received <= 0) {
      return abortUpload("500 Internal Server Error", "Upload receive failed");
    }
    remaining -= received;

    if (!isMultipart) {
      if (!writeOtaChunk(buffer, static_cast<size_t>(received))) {
        if (writeError == 2) {
          return abortUpload("500 Internal Server Error", "esp_ota_write failed");
        }
        return abortUpload("400 Bad Request", "Upload must contain raw ESP firmware bytes");
      }
      continue;
    }

    if (multipartFinished) continue;

    pending.insert(pending.end(), buffer, buffer + received);

    if (!multipartStarted) {
      static const std::string headerEnd = "\r\n\r\n";
      const int headerEndPos = findSequence(pending, headerEnd);
      if (headerEndPos < 0) {
        if (pending.size() > 8192) {
          return abortUpload("400 Bad Request", "Invalid multipart upload");
        }
        continue;
      }
      pending.erase(pending.begin(),
                    pending.begin() + headerEndPos + headerEnd.size());
      multipartStarted = true;
    }

    const int delimiterPos = findSequence(pending, multipartDelimiter);
    if (delimiterPos >= 0) {
      if (!writeOtaChunk(pending.data(), static_cast<size_t>(delimiterPos))) {
        if (writeError == 2) {
          return abortUpload("500 Internal Server Error", "esp_ota_write failed");
        }
        return abortUpload("400 Bad Request", "Upload must contain raw ESP firmware bytes");
      }
      pending.clear();
      multipartFinished = true;
      continue;
    }

    const size_t keepTail = multipartDelimiter.empty() ? 0 : multipartDelimiter.size() - 1;
    if (pending.size() > keepTail) {
      const size_t writable = pending.size() - keepTail;
      if (!writeOtaChunk(pending.data(), writable)) {
        if (writeError == 2) {
          return abortUpload("500 Internal Server Error", "esp_ota_write failed");
        }
        return abortUpload("400 Bad Request", "Upload must contain raw ESP firmware bytes");
      }
      pending.erase(pending.begin(), pending.begin() + writable);
    }
  }

  if (isMultipart && (!multipartStarted || !multipartFinished)) {
    return abortUpload("400 Bad Request", "Invalid multipart upload");
  }

  esp_err_t endResult = esp_ota_end(uploadHandle_);
  wdt_feed();
  if (endResult != ESP_OK) {
    sendResponse(req, "500 Internal Server Error", "text/plain",
                 String("esp_ota_end failed: ") + esp_err_to_name(endResult));
    return ESP_OK;
  }

  esp_err_t partitionResult = esp_ota_set_boot_partition(uploadPartition_);
  if (partitionResult != ESP_OK) {
    sendResponse(req, "500 Internal Server Error", "text/plain",
                 String("esp_ota_set_boot_partition failed: ") +
                     esp_err_to_name(partitionResult));
    return ESP_OK;
  }

  logger.info("Upload completed: " + String(uploadBytesReceived_) + " bytes");
  sendAndRestart(req, "Upgrade uploaded. Restarting...");
  return ESP_OK;
}

esp_err_t UpgradeManager::handleStatusTrampoline(httpd_req_t* req) {
  return static_cast<UpgradeManager*>(req->user_ctx)->handleStatus(req);
}

esp_err_t UpgradeManager::handleStatus(httpd_req_t* req) {
  cJSON* doc = cJSON_CreateObject();
  cJSON_AddBoolToObject(doc, "ready", true);
  cJSON_AddBoolToObject(doc, "upgrading", false);
  char* printed = cJSON_PrintUnformatted(doc);
  String payload = printed != nullptr ? String(printed) : String("{}");
  if (printed != nullptr) cJSON_free(printed);
  cJSON_Delete(doc);
  sendResponse(req, "200 OK", "application/json;charset=utf-8", payload);
  return ESP_OK;
}

bool UpgradeManager::performHttpUpgrade(const String& url, String& error) {
  wdt_feed();
  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  if (partition == nullptr) {
    error = "No OTA partition available";
    return false;
  }

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.timeout_ms = 20000;
  config.user_agent = "esp-ebus-upgrader/1.0";

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    error = "esp_http_client_init failed";
    return false;
  }

  esp_err_t openResult = esp_http_client_open(client, 0);
  if (openResult != ESP_OK) {
    error = String("esp_http_client_open failed: ") + esp_err_to_name(openResult);
    esp_http_client_cleanup(client);
    return false;
  }

  int headerRet = esp_http_client_fetch_headers(client);
  int statusCode = esp_http_client_get_status_code(client);
  int contentLength = esp_http_client_get_content_length(client);
  bool isChunked = esp_http_client_is_chunked_response(client);
  logger.debug("Upgrade HTTP status=" + String(statusCode) +
               " headers=" + String(headerRet) +
               " content_length=" + String(contentLength) +
               " chunked=" + String(isChunked ? 1 : 0));
  if (statusCode != 200) {
    error = String("Unexpected HTTP status: ") + String(statusCode);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  esp_ota_handle_t handle = 0;
  esp_err_t beginResult = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &handle);
  if (beginResult != ESP_OK) {
    error = String("esp_ota_begin failed: ") + esp_err_to_name(beginResult);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  uint8_t buffer[kOtaBufferSize];
  bool ok = true;
  size_t totalWritten = 0;
  bool checkedMagic = false;
  int nextProgressPercent = 10;
  size_t nextProgressBytes = kProgressStepBytes;

  logger.info("HTTP upgrade download started: url=" + url +
              ", content_length=" + String(contentLength) +
              ", chunked=" + String(isChunked ? 1 : 0));

  while (true) {
    int bytesRead = esp_http_client_read(client, reinterpret_cast<char*>(buffer),
                                         sizeof(buffer));
    wdt_feed();
    if (bytesRead < 0) {
      error = "esp_http_client_read failed";
      ok = false;
      break;
    }
    if (bytesRead == 0) {
      break;
    }

    if (!checkedMagic) {
      checkedMagic = true;
      if (buffer[0] != kEspImageMagic) {
        error = String("Downloaded file is not an ESP firmware image (magic=0x") +
                String(buffer[0], HEX) + ")";
        ok = false;
        break;
      }
    }

    esp_err_t writeResult = esp_ota_write(handle, buffer, bytesRead);
    wdt_feed();
    if (writeResult != ESP_OK) {
      error = String("esp_ota_write failed: ") + esp_err_to_name(writeResult);
      ok = false;
      break;
    }
    totalWritten += static_cast<size_t>(bytesRead);
    if (contentLength > 0) {
      int percent = static_cast<int>((totalWritten * 100) / contentLength);
      if (percent >= nextProgressPercent) {
        logger.info("HTTP upgrade progress " + String(percent) + "% (" +
                    String(totalWritten) + "/" + String(contentLength) + " bytes)");
        while (percent >= nextProgressPercent && nextProgressPercent < 100) {
          nextProgressPercent += 10;
        }
      }
    } else if (totalWritten >= nextProgressBytes) {
      logger.info("HTTP upgrade progress " + String(totalWritten) + " bytes");
      while (totalWritten >= nextProgressBytes) {
        nextProgressBytes += kProgressStepBytes;
      }
    }
    delay(1);
  }

  if (ok && contentLength > 0 && static_cast<int>(totalWritten) != contentLength) {
    error = String("Downloaded size mismatch: got ") + String(totalWritten) +
            ", expected " + String(contentLength);
    ok = false;
  }
  if (ok && !esp_http_client_is_complete_data_received(client)) {
    error = "HTTP download incomplete";
    ok = false;
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (!ok) {
    esp_ota_abort(handle);
    return false;
  }

  if (totalWritten == 0) {
    esp_ota_abort(handle);
    error = "No firmware data downloaded";
    return false;
  }

  esp_err_t endResult = esp_ota_end(handle);
  wdt_feed();
  if (endResult != ESP_OK) {
    error = String("esp_ota_end failed: ") + esp_err_to_name(endResult);
    return false;
  }

  esp_err_t partitionResult = esp_ota_set_boot_partition(partition);
  if (partitionResult != ESP_OK) {
    error = String("esp_ota_set_boot_partition failed: ") +
            esp_err_to_name(partitionResult);
    return false;
  }

  logger.info("HTTP upgrade download completed: " + String(totalWritten) + " bytes");
  return true;
}

esp_err_t UpgradeManager::handleHttpUpgradeTrampoline(httpd_req_t* req) {
  return static_cast<UpgradeManager*>(req->user_ctx)->handleHttpUpgrade(req);
}

esp_err_t UpgradeManager::handleHttpUpgrade(httpd_req_t* req) {
  preUpgradeDone_ = false;

  cJSON* doc = cJSON_Parse(readBody(req).c_str());
  if (doc == nullptr) {
    sendResponse(req, "400 Bad Request", "text/plain", "Invalid JSON payload");
    return ESP_OK;
  }

  cJSON* urlNode = cJSON_GetObjectItemCaseSensitive(doc, "url");
  String url = (cJSON_IsString(urlNode) && urlNode->valuestring != nullptr)
                   ? String(urlNode->valuestring)
                   : String("");
  if (url.isEmpty()) {
    cJSON_Delete(doc);
    sendResponse(req, "400 Bad Request", "text/plain", "Missing 'url'");
    return ESP_OK;
  }
  cJSON_Delete(doc);

  prepareForUpgrade();

  String error;
  if (!performHttpUpgrade(url, error)) {
    sendResponse(req, "500 Internal Server Error", "text/plain", error.c_str());
    return ESP_OK;
  }

  sendAndRestart(req, "Upgrade fetched. Restarting...");
  return ESP_OK;
}

void UpgradeManager::sendAndRestart(httpd_req_t* req, const char* message) {
  sendResponse(req, "200 OK", "text/plain", message);
  delay(1000);
  esp_restart();
}
