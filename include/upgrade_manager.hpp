#pragma once

#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include <ebus/types.hpp>
#include <functional>
#include <string>

class UpgradeManager {
 public:
  using PreUpgradeHook = std::function<void(void)>;

  static void begin();
  void setPreUpgradeHook(PreUpgradeHook hook);

  esp_err_t handleUpload(httpd_req_t* req);
  esp_err_t handleHttpUpgrade(httpd_req_t* req);
  static esp_err_t handleStatus(httpd_req_t* req);
  static void fetchStatus(const ebus::JsonChunkVisitor& visitor);

 private:
  static bool performHttpUpgrade(const std::string& url, std::string& error);
  void prepareForUpgrade();
  static void sendAndRestart(httpd_req_t* req, const char* message,
                             const char* id = "upgrade");
  void resetUploadState();

  PreUpgradeHook pre_upgrade_hook_;

  const esp_partition_t* upload_partition_ = nullptr;
  esp_ota_handle_t upload_handle_ = 0;
  bool pre_upgrade_done_ = false;
  size_t upload_bytes_received_ = 0;
  int upload_next_progress_percent_ = 10;
};
