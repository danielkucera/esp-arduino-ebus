#pragma once

#include <Arduino.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include <functional>

class UpgradeManager {
 public:
  using PreUpgradeHook = std::function<void(void)>;

  void begin();
  void setPreUpgradeHook(PreUpgradeHook hook);

  esp_err_t handleUpload(httpd_req_t* req);
  esp_err_t handleHttpUpgrade(httpd_req_t* req);
  esp_err_t handleStatus(httpd_req_t* req);

 private:
  bool performHttpUpgrade(const String& url, String& error);
  void prepareForUpgrade();
  void sendAndRestart(httpd_req_t* req, const char* message);
  void resetUploadState();

  PreUpgradeHook preUpgradeHook_;

  const esp_partition_t* uploadPartition_ = nullptr;
  esp_ota_handle_t uploadHandle_ = 0;
  bool preUpgradeDone_ = false;
  size_t uploadBytesReceived_ = 0;
  int uploadNextProgressPercent_ = 10;
};
