#include "EspOtaManager.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <unistd.h>

#include <string>

#ifdef INADDR_NONE
#undef INADDR_NONE
#endif

#include "Logger.hpp"
#include "main.hpp"

namespace {
constexpr size_t kOtaBufferSize = 1024;
constexpr uint8_t kEspImageMagic = 0xE9;
constexpr int kEspOtaFlashCommand = 0;
constexpr uint32_t kEspOtaTransferTimeoutMs = 60000;
constexpr uint32_t kEspOtaTaskDelayMs = 10;
constexpr uint32_t kEspOtaTaskStackSize = 8192;

std::string toHexByte(uint8_t value) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%02x", value);
  return std::string(buffer);
}
}  // namespace

void EspOtaManager::begin(uint16_t port) {
  port_ = port;
  if (udpSock_ >= 0) {
    close(udpSock_);
    udpSock_ = -1;
  }

  udpSock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udpSock_ < 0) {
    logger.error("ESPOTA: failed to create UDP socket");
    return;
  }

  timeval tv{};
  tv.tv_sec = 0;
  tv.tv_usec = 1000;
  setsockopt(udpSock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(udpSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    logger.error("ESPOTA: failed to bind UDP port " +
                 std::to_string(port_) + " errno=" + std::to_string(errno));
    close(udpSock_);
    udpSock_ = -1;
    return;
  }

  logger.info("ESPOTA: listening on UDP port " + std::to_string(port_));

  if (taskHandle_ == nullptr) {
    BaseType_t taskResult =
        xTaskCreate(taskEntry, "espota_task", kEspOtaTaskStackSize, this, 1,
                    &taskHandle_);
    if (taskResult != pdPASS) {
      logger.error("ESPOTA: failed to start task");
      taskHandle_ = nullptr;
    } else {
      logger.info("ESPOTA: task started");
    }
  }
}

void EspOtaManager::setPreUpgradeHook(PreUpgradeHook hook) {
  preUpgradeHook_ = hook;
}

void EspOtaManager::prepareForUpgrade() {
  if (!preUpgradeDone_ && preUpgradeHook_) {
    preUpgradeHook_();
    preUpgradeDone_ = true;
  }
}

void EspOtaManager::taskEntry(void* param) {
  EspOtaManager* self = static_cast<EspOtaManager*>(param);
  self->taskLoop();
}

void EspOtaManager::taskLoop() {
  while (true) {
    if (udpSock_ >= 0) {
      handleInvitation();
    }
    vTaskDelay(pdMS_TO_TICKS(kEspOtaTaskDelayMs));
  }
}

bool EspOtaManager::handleInvitation() {
  sockaddr_in remoteAddr = {};
  socklen_t remoteLen = sizeof(remoteAddr);
  int readLen = recvfrom(udpSock_, packet_, sizeof(packet_) - 1, 0,
                         reinterpret_cast<sockaddr*>(&remoteAddr), &remoteLen);
  if (readLen <= 0) return false;
  packet_[readLen] = '\0';

  int command = -1;
  unsigned int hostPort = 0;
  unsigned long expectedSizeRaw = 0;
  char md5[33] = {0};

  int parsed = sscanf(packet_, "%d %u %lu %32s", &command, &hostPort,
                      &expectedSizeRaw, md5);
  if (parsed < 3) {
    const char* msg = "ERROR: invalid invitation";
    sendto(udpSock_, msg, strlen(msg), 0,
           reinterpret_cast<const sockaddr*>(&remoteAddr), remoteLen);
    return false;
  }

  if (command != kEspOtaFlashCommand) {
    const char* msg = "ERROR: unsupported command";
    sendto(udpSock_, msg, strlen(msg), 0,
           reinterpret_cast<const sockaddr*>(&remoteAddr), remoteLen);
    return false;
  }

  size_t expectedSize = static_cast<size_t>(expectedSizeRaw);
  if (expectedSize == 0) {
    const char* msg = "ERROR: invalid size";
    sendto(udpSock_, msg, strlen(msg), 0,
           reinterpret_cast<const sockaddr*>(&remoteAddr), remoteLen);
    return false;
  }

  char remoteIp[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &remoteAddr.sin_addr, remoteIp, sizeof(remoteIp));
  logger.info("ESPOTA: invitation from " + std::string(remoteIp) + ":" +
              std::to_string(hostPort) + " size=" +
              std::to_string(expectedSize) + " md5=" + std::string(md5));

  const char* ok = "OK";
  sendto(udpSock_, ok, strlen(ok), 0,
         reinterpret_cast<const sockaddr*>(&remoteAddr), remoteLen);

  return performTransfer(remoteAddr, static_cast<uint16_t>(hostPort),
                         expectedSize);
}

bool EspOtaManager::performTransfer(const sockaddr_in& hostAddr, uint16_t hostPort,
                                    size_t expectedSize) {
  prepareForUpgrade();

  int tcpSock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (tcpSock < 0) {
    fail("cannot create TCP socket");
    return false;
  }

  sockaddr_in tcpAddr = hostAddr;
  tcpAddr.sin_port = htons(hostPort);
  if (connect(tcpSock, reinterpret_cast<sockaddr*>(&tcpAddr), sizeof(tcpAddr)) <
      0) {
    fail(std::string("connect failed errno=") + std::to_string(errno));
    close(tcpSock);
    return false;
  }

  timeval recvTimeout{};
  recvTimeout.tv_sec = 1;
  recvTimeout.tv_usec = 0;
  setsockopt(tcpSock, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));

  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  if (partition == nullptr) {
    fail("No OTA partition available");
    const char* msg = "ERROR[1]: no partition";
    send(tcpSock, msg, strlen(msg), 0);
    close(tcpSock);
    return false;
  }

  esp_ota_handle_t handle = 0;
  esp_err_t beginResult = esp_ota_begin(partition, expectedSize, &handle);
  if (beginResult != ESP_OK) {
    fail(std::string("esp_ota_begin failed: ") + esp_err_to_name(beginResult));
    const char* msg = "ERROR[2]: begin";
    send(tcpSock, msg, strlen(msg), 0);
    close(tcpSock);
    return false;
  }

  uint8_t buffer[kOtaBufferSize];
  size_t totalReceived = 0;
  bool checkedMagic = false;
  int nextProgressPercent = 10;
  uint32_t transferDeadline = (uint32_t)(esp_timer_get_time() / 1000ULL) + kEspOtaTransferTimeoutMs;

  while (totalReceived < expectedSize) {
    int bytesRead = recv(tcpSock, buffer, sizeof(buffer), 0);
    wdt_feed();
    if (bytesRead <= 0) {
      if (bytesRead == 0 || (uint32_t)(esp_timer_get_time() / 1000ULL) > transferDeadline) {
        esp_ota_abort(handle);
        fail("transfer timeout/disconnect");
        const char* msg = "ERROR[3]: timeout";
        send(tcpSock, msg, strlen(msg), 0);
        close(tcpSock);
        return false;
      }
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
      esp_ota_abort(handle);
      fail(std::string("recv failed errno=") + std::to_string(errno));
      const char* msg = "ERROR[3]: timeout";
      send(tcpSock, msg, strlen(msg), 0);
      close(tcpSock);
      return false;
    }

    transferDeadline = (uint32_t)(esp_timer_get_time() / 1000ULL) + kEspOtaTransferTimeoutMs;

    if (!checkedMagic) {
      checkedMagic = true;
      if (buffer[0] != kEspImageMagic) {
        esp_ota_abort(handle);
        fail(std::string("invalid firmware magic 0x") + toHexByte(buffer[0]));
        const char* msg = "ERROR[4]: bad image";
        send(tcpSock, msg, strlen(msg), 0);
        close(tcpSock);
        return false;
      }
    }

    esp_err_t writeResult = esp_ota_write(handle, buffer, bytesRead);
    wdt_feed();
    if (writeResult != ESP_OK) {
      esp_ota_abort(handle);
      fail(std::string("esp_ota_write failed: ") + esp_err_to_name(writeResult));
      const char* msg = "ERROR[5]: write";
      send(tcpSock, msg, strlen(msg), 0);
      close(tcpSock);
      return false;
    }

    totalReceived += static_cast<size_t>(bytesRead);
    int percent = static_cast<int>((totalReceived * 100) / expectedSize);
    if (percent >= nextProgressPercent) {
      logger.info("ESPOTA progress " + std::to_string(percent) + "% (" +
                  std::to_string(totalReceived) + "/" +
                  std::to_string(expectedSize) + " bytes)");
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
    fail(std::string("esp_ota_end failed: ") + esp_err_to_name(endResult));
    const char* msg = "ERROR[6]: end";
    send(tcpSock, msg, strlen(msg), 0);
    close(tcpSock);
    return false;
  }

  esp_err_t bootResult = esp_ota_set_boot_partition(partition);
  if (bootResult != ESP_OK) {
    fail(std::string("esp_ota_set_boot_partition failed: ") +
         esp_err_to_name(bootResult));
    const char* msg = "ERROR[7]: boot";
    send(tcpSock, msg, strlen(msg), 0);
    close(tcpSock);
    return false;
  }

  logger.info("ESPOTA: received " + std::to_string(totalReceived) +
              " bytes, rebooting");
  send(tcpSock, "OK", 2, 0);
  close(tcpSock);
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return true;
}

void EspOtaManager::fail(const std::string& reason) {
  logger.error("ESPOTA failure: " + reason);
}
