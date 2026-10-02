#include "system/esp_ota_manager.hpp"

#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef INADDR_NONE
#undef INADDR_NONE
#endif

#include "app/app_limits.hpp"
#include "main.hpp"
#include "system/logger.hpp"

namespace {
constexpr size_t ota_buffer_size = 1024;
constexpr uint8_t esp_image_magic = 0xE9;
constexpr int esp_ota_flash_command = 0;
constexpr uint32_t esp_ota_transfer_timeout_ms = 60000;
constexpr uint32_t esp_ota_task_delay_ms = 10;

std::string toHexByte(uint8_t value) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%02x", value);
  return std::string(buffer);
}
}  // namespace

void EspOtaManager::begin(uint16_t port) {
  port_ = port;
  if (udp_sock_ >= 0) {
    close(udp_sock_);
    udp_sock_ = -1;
  }

  udp_sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udp_sock_ < 0) {
    logger.error("ESPOTA: failed to create UDP socket");
    return;
  }

  timeval tv{};
  tv.tv_sec = 0;
  tv.tv_usec = 1000;
  setsockopt(udp_sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(udp_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    logger.error("ESPOTA: failed to bind UDP port " + std::to_string(port_) +
                 " errno=" + std::to_string(errno));
    close(udp_sock_);
    udp_sock_ = -1;
    return;
  }

  logger.info("ESPOTA: listening on UDP port " + std::to_string(port_));

  if (task_handle_ == nullptr) {
    BaseType_t task_result =
        xTaskCreate(taskEntry, "espota", app::limits::Task::espota_stack, this,
                    app::limits::Task::espota_priority, &task_handle_);
    if (task_result != pdPASS) {
      logger.error("ESPOTA: failed to start task");
      task_handle_ = nullptr;
    } else {
      logger.info("ESPOTA: task started");
    }
  }
}

void EspOtaManager::setPreUpgradeHook(PreUpgradeHook hook) {
  pre_upgrade_hook_ = hook;
}

void EspOtaManager::prepareForUpgrade() {
  if (!pre_upgrade_done_ && pre_upgrade_hook_) {
    pre_upgrade_hook_();
    pre_upgrade_done_ = true;
  }
}

void EspOtaManager::taskEntry(void* param) {
  EspOtaManager* self = static_cast<EspOtaManager*>(param);
  self->taskLoop();
}

void EspOtaManager::taskLoop() {
  while (true) {
    if (udp_sock_ >= 0) {
      handleInvitation();
    } else if (port_ > 0) {
      begin(port_);
    }
    vTaskDelay(pdMS_TO_TICKS(esp_ota_task_delay_ms));
  }
}

bool EspOtaManager::handleInvitation() {
  sockaddr_in remote_addr = {};
  socklen_t remote_len = sizeof(remote_addr);
  int read_len =
      recvfrom(udp_sock_, packet_, sizeof(packet_) - 1, 0,
               reinterpret_cast<sockaddr*>(&remote_addr), &remote_len);
  if (read_len <= 0) return false;
  packet_[read_len] = '\0';

  int command = -1;
  unsigned int host_port = 0;
  unsigned long expected_size_raw = 0;
  char md5[33] = {0};

  int parsed = sscanf(packet_, "%d %u %lu %32s", &command, &host_port,
                      &expected_size_raw, md5);
  if (parsed < 3) {
    const char* msg = "ERROR: invalid invitation";
    sendto(udp_sock_, msg, strlen(msg), 0,
           reinterpret_cast<const sockaddr*>(&remote_addr), remote_len);
    return false;
  }

  if (command != esp_ota_flash_command) {
    const char* msg = "ERROR: unsupported command";
    sendto(udp_sock_, msg, strlen(msg), 0,
           reinterpret_cast<const sockaddr*>(&remote_addr), remote_len);
    return false;
  }

  size_t expected_size = static_cast<size_t>(expected_size_raw);
  if (expected_size == 0) {
    const char* msg = "ERROR: invalid size";
    sendto(udp_sock_, msg, strlen(msg), 0,
           reinterpret_cast<const sockaddr*>(&remote_addr), remote_len);
    return false;
  }

  char remote_ip[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &remote_addr.sin_addr, remote_ip, sizeof(remote_ip));
  logger.info("ESPOTA: invitation from " + std::string(remote_ip) + ":" +
              std::to_string(host_port) + " size=" +
              std::to_string(expected_size) + " md5=" + std::string(md5));

  const char* ok = "OK";
  sendto(udp_sock_, ok, strlen(ok), 0,
         reinterpret_cast<const sockaddr*>(&remote_addr), remote_len);

  return performTransfer(remote_addr, static_cast<uint16_t>(host_port),
                         expected_size);
}

bool EspOtaManager::performTransfer(const sockaddr_in& host_addr,
                                    uint16_t host_port, size_t expected_size) {
  prepareForUpgrade();

  if (udp_sock_ >= 0) {
    close(udp_sock_);
    udp_sock_ = -1;
  }

  int tcp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (tcp_sock < 0) {
    fail(std::string("cannot create TCP socket errno=") +
         std::to_string(errno));
    return false;
  }

  sockaddr_in tcp_addr = host_addr;
  tcp_addr.sin_port = htons(host_port);
  if (connect(tcp_sock, reinterpret_cast<sockaddr*>(&tcp_addr),
              sizeof(tcp_addr)) < 0) {
    fail(std::string("connect failed errno=") + std::to_string(errno));
    close(tcp_sock);
    return false;
  }

  timeval recv_timeout{};
  recv_timeout.tv_sec = 1;
  recv_timeout.tv_usec = 0;
  setsockopt(tcp_sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout,
             sizeof(recv_timeout));

  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  if (partition == nullptr) {
    fail("No OTA partition available");
    const char* msg = "ERROR[1]: no partition";
    send(tcp_sock, msg, strlen(msg), 0);
    close(tcp_sock);
    return false;
  }

  esp_ota_handle_t handle = 0;
  esp_err_t begin_result = esp_ota_begin(partition, expected_size, &handle);
  if (begin_result != ESP_OK) {
    fail(std::string("esp_ota_begin failed: ") + esp_err_to_name(begin_result));
    const char* msg = "ERROR[2]: begin";
    send(tcp_sock, msg, strlen(msg), 0);
    close(tcp_sock);
    return false;
  }

  uint8_t buffer[ota_buffer_size];
  size_t total_received = 0;
  bool checked_magic = false;
  int next_progress_percent = 10;
  uint32_t transfer_deadline =
      (uint32_t)(esp_timer_get_time() / 1000ULL) + esp_ota_transfer_timeout_ms;

  while (total_received < expected_size) {
    int bytes_read = recv(tcp_sock, buffer, sizeof(buffer), 0);
    if (bytes_read <= 0) {
      if (bytes_read == 0 ||
          (uint32_t)(esp_timer_get_time() / 1000ULL) > transfer_deadline) {
        esp_ota_abort(handle);
        fail("transfer timeout/disconnect");
        const char* msg = "ERROR[3]: timeout";
        send(tcp_sock, msg, strlen(msg), 0);
        close(tcp_sock);
        return false;
      }
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        vTaskDelay(1);
        continue;
      }
      esp_ota_abort(handle);
      fail(std::string("recv failed errno=") + std::to_string(errno));
      const char* msg = "ERROR[3]: timeout";
      send(tcp_sock, msg, strlen(msg), 0);
      close(tcp_sock);
      return false;
    }

    transfer_deadline = (uint32_t)(esp_timer_get_time() / 1000ULL) +
                        esp_ota_transfer_timeout_ms;

    if (!checked_magic) {
      checked_magic = true;
      if (buffer[0] != esp_image_magic) {
        esp_ota_abort(handle);
        fail(std::string("invalid firmware magic 0x") + toHexByte(buffer[0]));
        const char* msg = "ERROR[4]: bad image";
        send(tcp_sock, msg, strlen(msg), 0);
        close(tcp_sock);
        return false;
      }
    }

    esp_err_t write_result = esp_ota_write(handle, buffer, bytes_read);
    if (write_result != ESP_OK) {
      esp_ota_abort(handle);
      fail(std::string("esp_ota_write failed: ") +
           esp_err_to_name(write_result));
      const char* msg = "ERROR[5]: write";
      send(tcp_sock, msg, strlen(msg), 0);
      close(tcp_sock);
      return false;
    }

    total_received += static_cast<size_t>(bytes_read);
    int percent = static_cast<int>((total_received * 100) / expected_size);
    if (percent >= next_progress_percent) {
      logger.info("ESPOTA progress " + std::to_string(percent) + "% (" +
                  std::to_string(total_received) + "/" +
                  std::to_string(expected_size) + " bytes)");
      while (percent >= next_progress_percent && next_progress_percent < 100) {
        next_progress_percent += 10;
      }
    }
    char ack[16];
    int ack_len = snprintf(ack, sizeof(ack), "%u\n",
                           static_cast<unsigned int>(total_received));
    send(tcp_sock, ack, ack_len, 0);
  }

  esp_err_t end_result = esp_ota_end(handle);
  if (end_result != ESP_OK) {
    fail(std::string("esp_ota_end failed: ") + esp_err_to_name(end_result));
    const char* msg = "ERROR[6]: end";
    send(tcp_sock, msg, strlen(msg), 0);
    close(tcp_sock);
    return false;
  }

  esp_err_t boot_result = esp_ota_set_boot_partition(partition);
  if (boot_result != ESP_OK) {
    fail(std::string("esp_ota_set_boot_partition failed: ") +
         esp_err_to_name(boot_result));
    const char* msg = "ERROR[7]: boot";
    send(tcp_sock, msg, strlen(msg), 0);
    close(tcp_sock);
    return false;
  }

  logger.info("ESPOTA: received " + std::to_string(total_received) +
              " bytes, rebooting");
  send(tcp_sock, "OK\n", 3, 0);
  close(tcp_sock);
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return true;
}

void EspOtaManager::fail(const std::string& reason) {
  char buf[128];
  snprintf(buf, sizeof(buf), "ESPOTA failure: %s", reason.c_str());
  logger.error(buf);
}
