#pragma once

#include <WebServer.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <functional>

class UpgradeManager {
 public:
  using PreUpgradeHook = std::function<void(void)>;

  void begin(WebServer* server);
  void beginEspOta(uint16_t port = 3232);
  void setPreUpgradeHook(PreUpgradeHook hook);

 private:
  void handleUploadChunk();
  void handleUploadFinished();
  void handleHttpUpgrade();
  void handleStatus();

  bool performHttpUpgrade(const String& url, String& error);
  void prepareForUpgrade();
  void sendAndRestart(const char* message);
  void resetUploadState();
  bool handleEspOtaInvitation();
  bool performEspOtaTransfer(const sockaddr_in& hostAddr, uint16_t hostPort,
                             size_t expectedSize);
  void failEspOta(const String& reason);
  static void espOtaTaskEntry(void* param);
  void espOtaTaskLoop();

  WebServer* server_ = nullptr;
  PreUpgradeHook preUpgradeHook_;

  const esp_partition_t* uploadPartition_ = nullptr;
  esp_ota_handle_t uploadHandle_ = 0;
  bool uploadHasError_ = false;
  bool uploadCompleted_ = false;
  String uploadErrorMessage_;
  bool preUpgradeDone_ = false;
  size_t uploadBytesReceived_ = 0;
  int uploadNextProgressPercent_ = 10;

  int espOtaUdpSock_ = -1;
  uint16_t espOtaPort_ = 3232;
  char espOtaPacket_[192] = {0};
  TaskHandle_t espOtaTaskHandle_ = nullptr;
};
