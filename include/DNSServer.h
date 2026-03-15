#pragma once

#include <cstdint>
#include <esp_netif_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>

class DNSServer {
 public:
  DNSServer();
  ~DNSServer();

  bool start(uint16_t port, const char* domainName,
             const esp_ip4_addr_t& resolvedIp);
  void stop();

 private:
  static void taskEntry(void* arg);
  void taskLoop();
  void processNextRequest();

  int socketFd_ = -1;
  uint16_t port_ = 0;
  std::string domain_;
  esp_ip4_addr_t resolvedIp_{};
  TaskHandle_t taskHandle_ = nullptr;
};
