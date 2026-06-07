#include "UpgradeManager.hpp"

#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>
#include <string>

#ifdef INADDR_NONE
#undef INADDR_NONE
#endif

#include "HttpUtils.hpp"
#include "Logger.hpp"
#include "http.hpp"
#include "main.hpp"

extern UpgradeManager upgradeManager;

namespace {
constexpr size_t kOtaBufferSize = 1024;
constexpr uint8_t kEspImageMagic = 0xE9;
constexpr size_t kProgressStepBytes = 64 * 1024;
}  // namespace

namespace {
esp_err_t handleUpgradeStatus(httpd_req_t* req) {
  return upgradeManager.handleStatus(req);
}

esp_err_t handleUpgradeHttp(httpd_req_t* req) {
  return upgradeManager.handleHttpUpgrade(req);
}

esp_err_t handleUpgradeUpload(httpd_req_t* req) {
  return upgradeManager.handleUpload(req);
}
}  // namespace

void UpgradeManager::begin() {
  RegisterUri("/api/v1/upgrade/status", HTTP_GET, handleUpgradeStatus);
  RegisterUri("/api/v1/upgrade/http", HTTP_POST, handleUpgradeHttp);
  RegisterUri("/api/v1/upgrade/upload", HTTP_POST, handleUpgradeUpload);
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

esp_err_t UpgradeManager::handleUpload(httpd_req_t* req) {
  resetUploadState();
  prepareForUpgrade();

  if (req->content_len <= 0) {
    httpd_resp_set_status(req, "411 Length Required");
    httpd_resp_set_type(req, "application/json;charset=utf-8");
    httpd_resp_sendstr_chunk(
        req,
        "{\"id\":\"upgrade_upload\",\"status\":\"failed\",\"error\":\"Content-"
        "Length required\"}");
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
  }

  uploadPartition_ = esp_ota_get_next_update_partition(nullptr);
  if (uploadPartition_ == nullptr) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json;charset=utf-8");
    httpd_resp_sendstr_chunk(
        req,
        "{\"id\":\"upgrade_upload\",\"status\":\"failed\",\"error\":\"No OTA "
        "partition available\"}");
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
  }

  esp_err_t beginResult =
      esp_ota_begin(uploadPartition_, OTA_SIZE_UNKNOWN, &uploadHandle_);
  if (beginResult != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json;charset=utf-8");
    {
      ebus::detail::JsonWriter writer([req](std::string_view chunk) {
        httpd_resp_send_chunk(req, chunk.data(), chunk.size());
      });
      char hex[12];
      snprintf(hex, sizeof(hex), "%02x", beginResult);
      auto root = writer.objectScope();
      writer.writeField("id", "upgrade_upload");
      writer.writeField("status", "failed");
      std::string err = "esp_ota_begin failed: ";
      err += esp_err_to_name(beginResult);
      err += " (0x";
      err += hex;
      err += ")";
      writer.writeField("error", err);
    }
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
  }

  uint8_t buffer[kOtaBufferSize];
  bool checkedMagic = false;
  int remaining = req->content_len;
  int writeError = 0;  // 1=invalid_magic, 2=ota_write_failed
  size_t nextProgressBytes = kProgressStepBytes;

  logger.info("Upload started: content_len=" +
              std::to_string(req->content_len));

  auto abortUpload = [&](const char* status, const char* message) -> esp_err_t {
    esp_ota_abort(uploadHandle_);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json;charset=utf-8");
    {
      ebus::detail::JsonWriter writer([req](std::string_view chunk) {
        httpd_resp_send_chunk(req, chunk.data(), chunk.size());
      });
      auto root = writer.objectScope();
      writer.writeField("id", "upgrade_upload");
      writer.writeField("status", "failed");
      writer.writeField("error", message);
    }
    httpd_resp_send_chunk(req, nullptr, 0);
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
    if (writeResult != ESP_OK) {
      writeError = 2;
      return false;
    }

    if (req->content_len > 0) {
      int percent =
          static_cast<int>((uploadBytesReceived_ * 100) / req->content_len);
      if (percent >= uploadNextProgressPercent_) {
        logger.info("Upload progress " + std::to_string(percent) + "% (" +
                    std::to_string(uploadBytesReceived_) + "/" +
                    std::to_string(req->content_len) + " bytes)");
        while (percent >= uploadNextProgressPercent_ &&
               uploadNextProgressPercent_ < 100) {
          uploadNextProgressPercent_ += 10;
        }
      }
    } else if (uploadBytesReceived_ >= nextProgressBytes) {
      logger.info("Upload progress " + std::to_string(uploadBytesReceived_) +
                  " bytes");
      while (uploadBytesReceived_ >= nextProgressBytes) {
        nextProgressBytes += kProgressStepBytes;
      }
    }
    return true;
  };

  while (remaining > 0) {
    int toRead = remaining > static_cast<int>(sizeof(buffer)) ? sizeof(buffer)
                                                              : remaining;
    int received = httpd_req_recv(req, reinterpret_cast<char*>(buffer), toRead);
    if (received <= 0) {
      return abortUpload("500 Internal Server Error", "Upload receive failed");
    }
    remaining -= received;

    if (!writeOtaChunk(buffer, static_cast<size_t>(received))) {
      if (writeError == 2) {
        return abortUpload("500 Internal Server Error", "esp_ota_write failed");
      }
      return abortUpload("400 Bad Request",
                         "Upload must contain raw ESP firmware bytes");
    }
  }

  esp_err_t endResult = esp_ota_end(uploadHandle_);
  if (endResult != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json;charset=utf-8");
    {
      ebus::detail::JsonWriter writer([req](std::string_view chunk) {
        httpd_resp_send_chunk(req, chunk.data(), chunk.size());
      });
      auto root = writer.objectScope();
      writer.writeField("id", "upgrade_upload");
      writer.writeField("status", "failed");
      writer.writeField("error", std::string("esp_ota_end failed: ") +
                                     esp_err_to_name(endResult));
    }
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
  }

  esp_err_t partitionResult = esp_ota_set_boot_partition(uploadPartition_);
  if (partitionResult != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json;charset=utf-8");
    {
      ebus::detail::JsonWriter writer([req](std::string_view chunk) {
        httpd_resp_send_chunk(req, chunk.data(), chunk.size());
      });
      auto root = writer.objectScope();
      writer.writeField("id", "upgrade_upload");
      writer.writeField("status", "failed");
      writer.writeField("error",
                        std::string("esp_ota_set_boot_partition failed: ") +
                            esp_err_to_name(partitionResult));
    }
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
  }

  logger.info("Upload completed: " + std::to_string(uploadBytesReceived_) +
              " bytes");
  sendAndRestart(req, "Upgrade uploaded. Restarting...", "upgrade_upload");
  return ESP_OK;
}

void UpgradeManager::fetchStatusJson(const ebus::JsonChunkVisitor& visitor) {
  ebus::detail::JsonWriter writer(visitor);
  auto root = writer.objectScope();
  writer.writeField("ready", true);
  writer.writeField("upgrading", false);
}

esp_err_t UpgradeManager::handleStatus(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  fetchStatusJson([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

bool UpgradeManager::performHttpUpgrade(const std::string& url,
                                        std::string& error) {
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
    error = std::string("esp_http_client_open failed: ") +
            esp_err_to_name(openResult);
    esp_http_client_cleanup(client);
    return false;
  }

  int headerRet = esp_http_client_fetch_headers(client);
  int statusCode = esp_http_client_get_status_code(client);
  int contentLength = esp_http_client_get_content_length(client);
  bool isChunked = esp_http_client_is_chunked_response(client);
  logger.debug("Upgrade HTTP status=" + std::to_string(statusCode) +
               " headers=" + std::to_string(headerRet) +
               " content_length=" + std::to_string(contentLength) +
               " chunked=" + std::to_string(isChunked ? 1 : 0));
  if (statusCode != 200) {
    error =
        std::string("Unexpected HTTP status: ") + std::to_string(statusCode);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  esp_ota_handle_t handle = 0;
  esp_err_t beginResult = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &handle);
  if (beginResult != ESP_OK) {
    error =
        std::string("esp_ota_begin failed: ") + esp_err_to_name(beginResult);
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
              ", content_length=" + std::to_string(contentLength) +
              ", chunked=" + std::to_string(isChunked ? 1 : 0));

  while (true) {
    int bytesRead = esp_http_client_read(
        client, reinterpret_cast<char*>(buffer), sizeof(buffer));
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
        char hex[4];
        snprintf(hex, sizeof(hex), "%02x", buffer[0]);
        error = "Downloaded file is not an ESP firmware image (magic=0x";
        error += hex;
        error += ")";
        ok = false;
        break;
      }
    }

    esp_err_t writeResult = esp_ota_write(handle, buffer, bytesRead);
    if (writeResult != ESP_OK) {
      error =
          std::string("esp_ota_write failed: ") + esp_err_to_name(writeResult);
      ok = false;
      break;
    }
    totalWritten += static_cast<size_t>(bytesRead);
    if (contentLength > 0) {
      int percent = static_cast<int>((totalWritten * 100) / contentLength);
      if (percent >= nextProgressPercent) {
        logger.info("HTTP upgrade progress " + std::to_string(percent) + "% (" +
                    std::to_string(totalWritten) + "/" +
                    std::to_string(contentLength) + " bytes)");
        while (percent >= nextProgressPercent && nextProgressPercent < 100) {
          nextProgressPercent += 10;
        }
      }
    } else if (totalWritten >= nextProgressBytes) {
      logger.info("HTTP upgrade progress " + std::to_string(totalWritten) +
                  " bytes");
      while (totalWritten >= nextProgressBytes) {
        nextProgressBytes += kProgressStepBytes;
      }
    }
    vTaskDelay(1);
  }

  if (ok && contentLength > 0 &&
      static_cast<int>(totalWritten) != contentLength) {
    error = std::string("Downloaded size mismatch: got ") +
            std::to_string(totalWritten) + ", expected " +
            std::to_string(contentLength);
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
  if (endResult != ESP_OK) {
    error = std::string("esp_ota_end failed: ") + esp_err_to_name(endResult);
    return false;
  }

  esp_err_t partitionResult = esp_ota_set_boot_partition(partition);
  if (partitionResult != ESP_OK) {
    error = std::string("esp_ota_set_boot_partition failed: ") +
            esp_err_to_name(partitionResult);
    return false;
  }

  logger.info("HTTP upgrade download completed: " +
              std::to_string(totalWritten) + " bytes");
  return true;
}

esp_err_t UpgradeManager::handleHttpUpgrade(httpd_req_t* req) {
  preUpgradeDone_ = false;
  std::string body = HttpUtils::readBody(req);
  ebus::detail::JsonReader reader(body);

  std::string url;
  if (reader.findKey("url")) {
    reader.next();
    url = std::string(reader.value());
  }

  if (url.empty()) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json;charset=utf-8");
    httpd_resp_sendstr_chunk(
        req,
        "{\"id\":\"upgrade_http\",\"status\":\"failed\",\"error\":\"Missing "
        "'url'\"}");
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
  }

  prepareForUpgrade();

  std::string error;
  if (!performHttpUpgrade(url, error)) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json;charset=utf-8");
    {
      ebus::detail::JsonWriter writer([req](std::string_view chunk) {
        httpd_resp_send_chunk(req, chunk.data(), chunk.size());
      });
      auto root = writer.objectScope();
      writer.writeField("id", "upgrade_http");
      writer.writeField("status", "failed");
      writer.writeField("error", error);
    }
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
  }

  sendAndRestart(req, "Upgrade fetched. Restarting...", "upgrade_http");
  return ESP_OK;
}

void UpgradeManager::sendAndRestart(httpd_req_t* req, const char* message,
                                    const char* id) {
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  {
    ebus::detail::JsonWriter writer([req](std::string_view chunk) {
      httpd_resp_send_chunk(req, chunk.data(), chunk.size());
    });
    auto root = writer.objectScope();
    writer.writeField("id", id);
    writer.writeField("status", "successful");
    writer.writeField("message", message);
  }
  httpd_resp_send_chunk(req, nullptr, 0);

  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
}
