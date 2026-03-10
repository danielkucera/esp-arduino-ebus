#pragma once

#include <Arduino.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include <functional>

#include "HttpUtils.hpp"

class UpgradeManager {
 public:
  using PreUpgradeHook = std::function<void(void)>;

  void begin(httpd_handle_t server);
  void setPreUpgradeHook(PreUpgradeHook hook);

 private:
  esp_err_t handleUpload(httpd_req_t* req);
  esp_err_t handleHttpUpgrade(httpd_req_t* req);
  esp_err_t handleStatus(httpd_req_t* req);

  template <typename T, esp_err_t (T::*Method)(httpd_req_t*)>
  friend esp_err_t HttpUtils::Trampoline(httpd_req_t* req);

  bool performHttpUpgrade(const String& url, String& error);
  void prepareForUpgrade();
  void sendAndRestart(httpd_req_t* req, const char* message);
  void resetUploadState();

  httpd_handle_t server_ = nullptr;
  PreUpgradeHook preUpgradeHook_;

  const esp_partition_t* uploadPartition_ = nullptr;
  esp_ota_handle_t uploadHandle_ = 0;
  bool preUpgradeDone_ = false;
  size_t uploadBytesReceived_ = 0;
  int uploadNextProgressPercent_ = 10;
};
