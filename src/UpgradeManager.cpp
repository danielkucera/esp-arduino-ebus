#include "UpgradeManager.hpp"

#include <ArduinoJson.h>
#include <cstring>
#include <cerrno>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <unistd.h>

#include "Logger.hpp"
#include "main.hpp"

namespace {
constexpr size_t kOtaBufferSize = 1024;
constexpr uint8_t kEspImageMagic = 0xE9;
constexpr int kEspOtaFlashCommand = 0;
constexpr uint32_t kEspOtaTransferTimeoutMs = 60000;
constexpr uint32_t kEspOtaTaskDelayMs = 10;
constexpr uint32_t kEspOtaTaskStackSize = 8192;
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

void UpgradeManager::beginEspOta(uint16_t port) {
  espOtaPort_ = port;
  if (espOtaUdpSock_ >= 0) {
    close(espOtaUdpSock_);
    espOtaUdpSock_ = -1;
  }

  espOtaUdpSock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (espOtaUdpSock_ < 0) {
    logger.error("ESPOTA: failed to create UDP socket");
    return;
  }

  timeval tv{};
  tv.tv_sec = 0;
  tv.tv_usec = 1000;
  setsockopt(espOtaUdpSock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(espOtaPort_);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(espOtaUdpSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    logger.error("ESPOTA: failed to bind UDP port " + String(espOtaPort_) +
                 " errno=" + String(errno));
    close(espOtaUdpSock_);
    espOtaUdpSock_ = -1;
    return;
  }

  logger.info("ESPOTA: listening on UDP port " + String(espOtaPort_));

  if (espOtaTaskHandle_ == nullptr) {
    BaseType_t taskResult =
        xTaskCreate(espOtaTaskEntry, "espota_task", kEspOtaTaskStackSize, this, 1,
                    &espOtaTaskHandle_);
    if (taskResult != pdPASS) {
      logger.error("ESPOTA: failed to start task");
      espOtaTaskHandle_ = nullptr;
    } else {
      logger.info("ESPOTA: task started");
    }
  }
}

void UpgradeManager::espOtaTaskEntry(void* param) {
  UpgradeManager* self = static_cast<UpgradeManager*>(param);
  self->espOtaTaskLoop();
}

void UpgradeManager::espOtaTaskLoop() {
  while (true) {
    if (espOtaUdpSock_ >= 0) {
      handleEspOtaInvitation();
    }
    vTaskDelay(pdMS_TO_TICKS(kEspOtaTaskDelayMs));
  }
}

bool UpgradeManager::handleEspOtaInvitation() {
  sockaddr_in remoteAddr = {};
  socklen_t remoteLen = sizeof(remoteAddr);
  int readLen = recvfrom(espOtaUdpSock_, espOtaPacket_, sizeof(espOtaPacket_) - 1, 0,
                         reinterpret_cast<sockaddr*>(&remoteAddr), &remoteLen);
  if (readLen <= 0) return false;
  espOtaPacket_[readLen] = '\0';

  int command = -1;
  unsigned int hostPort = 0;
  unsigned long expectedSizeRaw = 0;
  char md5[33] = {0};

  int parsed = sscanf(espOtaPacket_, "%d %u %lu %32s", &command, &hostPort,
                      &expectedSizeRaw, md5);
  if (parsed < 3) {
    const char* msg = "ERROR: invalid invitation";
    sendto(espOtaUdpSock_, msg, strlen(msg), 0,
           reinterpret_cast<const sockaddr*>(&remoteAddr), remoteLen);
    return false;
  }

  if (command != kEspOtaFlashCommand) {
    const char* msg = "ERROR: unsupported command";
    sendto(espOtaUdpSock_, msg, strlen(msg), 0,
           reinterpret_cast<const sockaddr*>(&remoteAddr), remoteLen);
    return false;
  }

  size_t expectedSize = static_cast<size_t>(expectedSizeRaw);
  if (expectedSize == 0) {
    const char* msg = "ERROR: invalid size";
    sendto(espOtaUdpSock_, msg, strlen(msg), 0,
           reinterpret_cast<const sockaddr*>(&remoteAddr), remoteLen);
    return false;
  }

  char remoteIp[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &remoteAddr.sin_addr, remoteIp, sizeof(remoteIp));
  logger.info("ESPOTA: invitation from " + String(remoteIp) + ":" +
              String(hostPort) + " size=" + String(expectedSize) + " md5=" +
              String(md5));

  const char* ok = "OK";
  sendto(espOtaUdpSock_, ok, strlen(ok), 0,
         reinterpret_cast<const sockaddr*>(&remoteAddr), remoteLen);

  return performEspOtaTransfer(remoteAddr, static_cast<uint16_t>(hostPort),
                               expectedSize);
}

bool UpgradeManager::performEspOtaTransfer(const sockaddr_in& hostAddr,
                                           uint16_t hostPort,
                                           size_t expectedSize) {
  prepareForUpgrade();

  int tcpSock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (tcpSock < 0) {
    failEspOta("cannot create TCP socket");
    return false;
  }

  sockaddr_in tcpAddr = hostAddr;
  tcpAddr.sin_port = htons(hostPort);
  if (connect(tcpSock, reinterpret_cast<sockaddr*>(&tcpAddr), sizeof(tcpAddr)) < 0) {
    failEspOta(String("connect failed errno=") + String(errno));
    close(tcpSock);
    return false;
  }

  timeval recvTimeout{};
  recvTimeout.tv_sec = 1;
  recvTimeout.tv_usec = 0;
  setsockopt(tcpSock, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));

  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  if (partition == nullptr) {
    failEspOta("No OTA partition available");
    const char* msg = "ERROR[1]: no partition";
    send(tcpSock, msg, strlen(msg), 0);
    close(tcpSock);
    return false;
  }

  esp_ota_handle_t handle = 0;
  esp_err_t beginResult = esp_ota_begin(partition, expectedSize, &handle);
  if (beginResult != ESP_OK) {
    failEspOta(String("esp_ota_begin failed: ") + esp_err_to_name(beginResult));
    const char* msg = "ERROR[2]: begin";
    send(tcpSock, msg, strlen(msg), 0);
    close(tcpSock);
    return false;
  }

  uint8_t buffer[kOtaBufferSize];
  size_t totalReceived = 0;
  bool checkedMagic = false;
  int nextProgressPercent = 10;
  uint32_t transferDeadline = millis() + kEspOtaTransferTimeoutMs;

  while (totalReceived < expectedSize) {
    int bytesRead = recv(tcpSock, buffer, sizeof(buffer), 0);
    if (bytesRead <= 0) {
      if (bytesRead == 0 || millis() > transferDeadline) {
        esp_ota_abort(handle);
        failEspOta("transfer timeout/disconnect");
        const char* msg = "ERROR[3]: timeout";
        send(tcpSock, msg, strlen(msg), 0);
        close(tcpSock);
        return false;
      }
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        delay(1);
        wdt_feed();
        continue;
      }
      esp_ota_abort(handle);
      failEspOta(String("recv failed errno=") + String(errno));
      const char* msg = "ERROR[3]: timeout";
      send(tcpSock, msg, strlen(msg), 0);
      close(tcpSock);
      return false;
    }

    transferDeadline = millis() + kEspOtaTransferTimeoutMs;
    wdt_feed();

    if (!checkedMagic) {
      checkedMagic = true;
      if (buffer[0] != kEspImageMagic) {
        esp_ota_abort(handle);
        failEspOta(String("invalid firmware magic 0x") + String(buffer[0], HEX));
        const char* msg = "ERROR[4]: bad image";
        send(tcpSock, msg, strlen(msg), 0);
        close(tcpSock);
        return false;
      }
    }

    esp_err_t writeResult = esp_ota_write(handle, buffer, bytesRead);
    if (writeResult != ESP_OK) {
      esp_ota_abort(handle);
      failEspOta(String("esp_ota_write failed: ") + esp_err_to_name(writeResult));
      const char* msg = "ERROR[5]: write";
      send(tcpSock, msg, strlen(msg), 0);
      close(tcpSock);
      return false;
    }

    totalReceived += static_cast<size_t>(bytesRead);
    int percent = static_cast<int>((totalReceived * 100) / expectedSize);
    if (percent >= nextProgressPercent) {
      logger.info("ESPOTA progress " + String(percent) + "% (" +
                  String(totalReceived) + "/" + String(expectedSize) + " bytes)");
      while (percent >= nextProgressPercent && nextProgressPercent < 100) {
        nextProgressPercent += 10;
      }
    }
    char ack[16];
    int ackLen = snprintf(ack, sizeof(ack), "%u",
                          static_cast<unsigned int>(totalReceived));
    send(tcpSock, ack, ackLen, 0);
    wdt_feed();
  }

  esp_err_t endResult = esp_ota_end(handle);
  if (endResult != ESP_OK) {
    failEspOta(String("esp_ota_end failed: ") + esp_err_to_name(endResult));
    const char* msg = "ERROR[6]: end";
    send(tcpSock, msg, strlen(msg), 0);
    close(tcpSock);
    return false;
  }

  esp_err_t bootResult = esp_ota_set_boot_partition(partition);
  if (bootResult != ESP_OK) {
    failEspOta(String("esp_ota_set_boot_partition failed: ") +
               esp_err_to_name(bootResult));
    const char* msg = "ERROR[7]: boot";
    send(tcpSock, msg, strlen(msg), 0);
    close(tcpSock);
    return false;
  }

  logger.info("ESPOTA: received " + String(totalReceived) + " bytes, rebooting");
  send(tcpSock, "OK", 2, 0);
  close(tcpSock);
  delay(5000);
  esp_restart();
  return true;
}

void UpgradeManager::failEspOta(const String& reason) {
  logger.error("ESPOTA failure: " + reason);
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
  delay(5000);
  esp_restart();
}
