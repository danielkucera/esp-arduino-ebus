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

 private:
  void prepareForUpgrade();
  bool handleInvitation();
  bool performTransfer(const sockaddr_in& hostAddr, uint16_t hostPort,
                       size_t expectedSize);
  void fail(const std::string& reason);
  static void taskEntry(void* param);
  void taskLoop();

  PreUpgradeHook preUpgradeHook_;
  bool preUpgradeDone_ = false;
  int udpSock_ = -1;
  uint16_t port_ = 3232;
  char packet_[192] = {0};
  TaskHandle_t taskHandle_ = nullptr;
};
