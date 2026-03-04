#include "UpgradeManager.hpp"

#include <cJSON.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "Logger.hpp"
#include "main.hpp"

namespace {
constexpr size_t kOtaBufferSize = 1024;
constexpr uint8_t kEspImageMagic = 0xE9;
}

void UpgradeManager::begin(WebServer* server) {
  server_ = server;

  server_->on("/api/v1/upgrade/status", HTTP_GET, [this]() { handleStatus(); });

  server_->on("/api/v1/upgrade/http", HTTP_POST, [this]() { handleHttpUpgrade(); });

  server_->on(
      "/api/v1/upgrade/upload", HTTP_POST,
      [this]() { handleUploadFinished(); },
      [this]() { handleUploadChunk(); });
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
  uploadHasError_ = false;
  uploadCompleted_ = false;
  uploadErrorMessage_ = "";
  preUpgradeDone_ = false;
  uploadBytesReceived_ = 0;
  uploadNextProgressPercent_ = 10;
}

void UpgradeManager::handleUploadChunk() {
  wdt_feed();
  HTTPUpload& upload = server_->upload();

  if (upload.status == UPLOAD_FILE_START) {
    resetUploadState();
    prepareForUpgrade();

    uploadPartition_ = esp_ota_get_next_update_partition(nullptr);
    if (uploadPartition_ == nullptr) {
      uploadHasError_ = true;
      uploadErrorMessage_ = "No OTA partition available";
      return;
    }

    esp_err_t beginResult =
        esp_ota_begin(uploadPartition_, OTA_SIZE_UNKNOWN, &uploadHandle_);
    if (beginResult != ESP_OK) {
      uploadHasError_ = true;
      uploadErrorMessage_ = String("esp_ota_begin failed: ") +
                            esp_err_to_name(beginResult);
    }
    return;
  }

  if (uploadHasError_) {
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    uploadBytesReceived_ += upload.currentSize;
    esp_err_t writeResult =
        esp_ota_write(uploadHandle_, upload.buf, upload.currentSize);
    wdt_feed();
    if (writeResult != ESP_OK) {
      uploadHasError_ = true;
      uploadErrorMessage_ = String("esp_ota_write failed: ") +
                            esp_err_to_name(writeResult);
    } else if (upload.totalSize > 0) {
      int percent = static_cast<int>((uploadBytesReceived_ * 100) / upload.totalSize);
      if (percent >= uploadNextProgressPercent_) {
        logger.info("Upload progress " + String(percent) + "% (" +
                    String(uploadBytesReceived_) + "/" + String(upload.totalSize) +
                    " bytes)");
        while (percent >= uploadNextProgressPercent_ &&
               uploadNextProgressPercent_ < 100) {
          uploadNextProgressPercent_ += 10;
        }
      }
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    esp_err_t endResult = esp_ota_end(uploadHandle_);
    wdt_feed();
    if (endResult != ESP_OK) {
      uploadHasError_ = true;
      uploadErrorMessage_ = String("esp_ota_end failed: ") +
                            esp_err_to_name(endResult);
      return;
    }

    esp_err_t partitionResult = esp_ota_set_boot_partition(uploadPartition_);
    if (partitionResult != ESP_OK) {
      uploadHasError_ = true;
      uploadErrorMessage_ = String("esp_ota_set_boot_partition failed: ") +
                            esp_err_to_name(partitionResult);
      return;
    }

    uploadCompleted_ = true;
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadHasError_ = true;
    uploadErrorMessage_ = "Upload aborted";
  }
}

void UpgradeManager::handleUploadFinished() {
  if (uploadCompleted_ && !uploadHasError_) {
    sendAndRestart("Upgrade uploaded. Restarting...");
    return;
  }

  if (uploadErrorMessage_.isEmpty()) {
    uploadErrorMessage_ = "Upgrade failed";
  }

  server_->send(500, "text/plain", uploadErrorMessage_);
}

void UpgradeManager::handleStatus() {
  cJSON* doc = cJSON_CreateObject();
  cJSON_AddBoolToObject(doc, "ready", true);
  cJSON_AddBoolToObject(doc, "upgrading", false);
  char* printed = cJSON_PrintUnformatted(doc);
  String payload = printed != nullptr ? String(printed) : String("{}");
  if (printed != nullptr) cJSON_free(printed);
  cJSON_Delete(doc);
  server_->send(200, "application/json;charset=utf-8", payload);
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
    }
    delay(1);
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

  return true;
}

void UpgradeManager::handleHttpUpgrade() {
  preUpgradeDone_ = false;

  cJSON* doc = cJSON_Parse(server_->arg("plain").c_str());
  if (doc == nullptr) {
    server_->send(400, "text/plain", "Invalid JSON payload");
    return;
  }

  cJSON* urlNode = cJSON_GetObjectItemCaseSensitive(doc, "url");
  String url = (cJSON_IsString(urlNode) && urlNode->valuestring != nullptr)
                   ? String(urlNode->valuestring)
                   : String("");
  if (url.isEmpty()) {
    cJSON_Delete(doc);
    server_->send(400, "text/plain", "Missing 'url'");
    return;
  }
  cJSON_Delete(doc);

  prepareForUpgrade();

  String error;
  if (!performHttpUpgrade(url, error)) {
    server_->send(500, "text/plain", error.c_str());
    return;
  }

  sendAndRestart("Upgrade fetched. Restarting...");
}

void UpgradeManager::sendAndRestart(const char* message) {
  server_->send(200, "text/plain", message);
  server_->client().flush();
  server_->client().stop();
  delay(1000);
  esp_restart();
}
