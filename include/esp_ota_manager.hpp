#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <functional>
#include <string>

class EspOtaManager {
 public:
  using PreUpgradeHook = std::function<void(void)>;

  void begin(uint16_t port = 3232);
  void setPreUpgradeHook(PreUpgradeHook hook);

  TaskHandle_t getTaskHandle() const { return task_handle_; }

 private:
  void prepareForUpgrade();
  bool handleInvitation();
  bool performTransfer(const sockaddr_in& hostAddr, uint16_t hostPort,
                       size_t expectedSize);
  static void fail(const std::string& reason);
  static void taskEntry(void* param);
  void taskLoop();

  PreUpgradeHook pre_upgrade_hook_;
  bool pre_upgrade_done_ = false;
  int udp_sock_ = -1;
  uint16_t port_ = 3232;
  char packet_[192] = {0};
  TaskHandle_t task_handle_ = nullptr;
};
