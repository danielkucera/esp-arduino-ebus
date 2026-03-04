#pragma once

#include <WebServer.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include <functional>

class UpgradeManager {
 public:
  using PreUpgradeHook = std::function<void(void)>;

  void begin(WebServer* server);
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
};
