#pragma once

#include <WebServer.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <lwip/sockets.h>

#include <functional>

class UpgradeManager {
 public:
  using PreUpgradeHook = std::function<void(void)>;

  void begin(WebServer* server);
  void beginEspOta(uint16_t port = 3232);
  void handleEspOta();
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

  WebServer* server_ = nullptr;
  PreUpgradeHook preUpgradeHook_;

  const esp_partition_t* uploadPartition_ = nullptr;
  esp_ota_handle_t uploadHandle_ = 0;
  bool uploadHasError_ = false;
  bool uploadCompleted_ = false;
  String uploadErrorMessage_;
  bool preUpgradeDone_ = false;

  int espOtaUdpSock_ = -1;
  uint16_t espOtaPort_ = 3232;
  char espOtaPacket_[192] = {0};
};
