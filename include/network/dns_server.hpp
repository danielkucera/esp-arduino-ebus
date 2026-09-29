#pragma once

#include <esp_netif_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <string>

class DNSServer {
 public:
  DNSServer();
  ~DNSServer();

  bool start(uint16_t port, const char* domain_name,
             const esp_ip4_addr_t& resolved_ip);
  void stop();

  TaskHandle_t getTaskHandle() const { return task_handle_; }

 private:
  static void taskEntry(void* arg);
  void taskLoop();
  void processNextRequest();

  int socket_fd_ = -1;
  uint16_t port_ = 0;
  std::string domain_;
  esp_ip4_addr_t resolved_ip_{};
  TaskHandle_t task_handle_ = nullptr;
  volatile bool running_ = false;
};
