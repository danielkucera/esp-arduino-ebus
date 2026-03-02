#include "UpgradeManager.hpp"

#include <ArduinoJson.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "main.hpp"

namespace {
constexpr size_t kOtaBufferSize = 1024;
}

void UpgradeManager::begin(WebServer* server) {
  server_ = server;

  server_->on("/api/v1/upgrade/status", HTTP_GET, [this]() { handleStatus(); });

  server_->on("/api/v1/upgrade/http", HTTP_POST, [this]() { handleHttpUpgrade(); });

  server_->on(
      "/api/v1/upgrade/upload", HTTP_POST,
      [this]() { handleUploadFinished(); },
      [this]() { handleUploadChunk(); });

  // Keep legacy endpoint used by upload_http.py and PlatformIO remote upload.
  server_->on(
      "/firmware", HTTP_POST,
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
    esp_err_t writeResult =
        esp_ota_write(uploadHandle_, upload.buf, upload.currentSize);
    wdt_feed();
    if (writeResult != ESP_OK) {
      uploadHasError_ = true;
      uploadErrorMessage_ = String("esp_ota_write failed: ") +
                            esp_err_to_name(writeResult);
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
  JsonDocument doc;
  doc["ready"] = true;
  doc["upgrading"] = false;
  String payload;
  serializeJson(doc, payload);
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

    esp_err_t writeResult = esp_ota_write(handle, buffer, bytesRead);
    wdt_feed();
    if (writeResult != ESP_OK) {
      error = String("esp_ota_write failed: ") + esp_err_to_name(writeResult);
      ok = false;
      break;
    }
    delay(1);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (!ok) {
    esp_ota_abort(handle);
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

  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, server_->arg("plain"));
  if (jsonError) {
    server_->send(400, "text/plain", "Invalid JSON payload");
    return;
  }

  String url = doc["url"].as<String>();
  if (url.isEmpty()) {
    server_->send(400, "text/plain", "Missing 'url'");
    return;
  }

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
  delay(200);
  esp_restart();
}
