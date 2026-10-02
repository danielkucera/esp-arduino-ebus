#include "system/upgrade_manager.hpp"

#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cerrno>
#include <cstdio>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>
#include <string>

#ifdef INADDR_NONE
#undef INADDR_NONE
#endif

#include "main.hpp"
#include "network/http.hpp"
#include "network/http_utils.hpp"
#include "system/device_status.hpp"
#include "system/logger.hpp"

namespace {
constexpr size_t ota_buffer_size = 1024;
constexpr uint8_t esp_image_magic = 0xE9;
constexpr size_t progress_step_bytes = 64 * 1024;
}  // namespace

namespace {
esp_err_t handleUpgradeStatus(httpd_req_t* req) {
  return UpgradeManager::instance_->handleStatus(req);
}

esp_err_t handleUpgradeHttp(httpd_req_t* req) {
  return UpgradeManager::instance_->handleHttpUpgrade(req);
}

esp_err_t handleUpgradeUpload(httpd_req_t* req) {
  return UpgradeManager::instance_->handleUpload(req);
}
}  // namespace

UpgradeManager* UpgradeManager::instance_ = nullptr;

UpgradeManager::UpgradeManager() { instance_ = this; }

void UpgradeManager::begin() {
  registerUri("/api/v1/upgrade/status", HTTP_GET, handleUpgradeStatus);
  registerUri("/api/v1/upgrade/http", HTTP_POST, handleUpgradeHttp);
  registerUri("/api/v1/upgrade/upload", HTTP_POST, handleUpgradeUpload);
}

void UpgradeManager::setPreUpgradeHook(PreUpgradeHook hook) {
  pre_upgrade_hook_ = hook;
}

void UpgradeManager::prepareForUpgrade() {
  if (!pre_upgrade_done_ && pre_upgrade_hook_) {
    pre_upgrade_hook_();
    pre_upgrade_done_ = true;
  }
}

void UpgradeManager::resetUploadState() {
  upload_partition_ = nullptr;
  upload_handle_ = 0;
  pre_upgrade_done_ = false;
  upload_bytes_received_ = 0;
  upload_next_progress_percent_ = 10;
}

esp_err_t UpgradeManager::handleUpload(httpd_req_t* req) {
  resetUploadState();
  prepareForUpgrade();

  if (req->content_len <= 0) {
    HttpUtils::sendErrorResponse(req, "411 Length Required", "upgrade_upload",
                                 "Content-Length required");
    return ESP_OK;
  }

  upload_partition_ = esp_ota_get_next_update_partition(nullptr);
  if (upload_partition_ == nullptr) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error",
                                 "upgrade_upload",
                                 "No OTA partition available");
    return ESP_OK;
  }

  esp_err_t begin_result =
      esp_ota_begin(upload_partition_, OTA_SIZE_UNKNOWN, &upload_handle_);
  if (begin_result != ESP_OK) {
    char hex[12];
    snprintf(hex, sizeof(hex), "%02x", begin_result);
    std::string error_msg = "esp_ota_begin failed: ";
    error_msg += esp_err_to_name(begin_result);
    error_msg += " (0x";
    error_msg += hex;
    error_msg += ")";
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error",
                                 "upgrade_upload", error_msg);
    return ESP_OK;
  }

  uint8_t buffer[ota_buffer_size];
  bool checked_magic = false;
  int remaining = req->content_len;
  int write_error = 0;  // 1=invalid_magic, 2=ota_write_failed
  size_t next_progress_bytes = progress_step_bytes;

  logger.info("Upload started: content_len=" +
              std::to_string(req->content_len));

  auto abort_upload = [&](const char* status,
                          std::string_view message) -> esp_err_t {
    esp_ota_abort(upload_handle_);
    HttpUtils::sendErrorResponse(req, status, "upgrade_upload", message);
    return ESP_OK;
  };

  auto write_ota_chunk = [&](const uint8_t* data, size_t len) -> bool {
    if (len == 0) return true;

    if (!checked_magic) {
      checked_magic = true;
      if (data[0] != esp_image_magic) {
        write_error = 1;
        return false;
      }
    }

    upload_bytes_received_ += len;
    esp_err_t write_result = esp_ota_write(upload_handle_, data, len);
    if (write_result != ESP_OK) {
      write_error = 2;
      return false;
    }

    if (req->content_len > 0) {
      int percent =
          static_cast<int>((upload_bytes_received_ * 100) / req->content_len);
      if (percent >= upload_next_progress_percent_) {
        logger.info("Upload progress " + std::to_string(percent) + "% (" +
                    std::to_string(upload_bytes_received_) + "/" +
                    std::to_string(req->content_len) + " bytes)");
        while (percent >= upload_next_progress_percent_ &&
               upload_next_progress_percent_ < 100) {
          upload_next_progress_percent_ += 10;
        }
      }
    } else if (upload_bytes_received_ >= next_progress_bytes) {
      logger.info("Upload progress " + std::to_string(upload_bytes_received_) +
                  " bytes");
      while (upload_bytes_received_ >= next_progress_bytes) {
        next_progress_bytes += progress_step_bytes;
      }
    }
    return true;
  };

  // Lossy links stall mid-transfer: tolerate receive gaps up to
  // upload_stall_budget_ms after the last byte instead of aborting on the
  // first socket timeout. Fatal socket errors still abort immediately.
  constexpr uint32_t upload_stall_budget_ms = 120000;
  uint32_t last_progress_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

  while (remaining > 0) {
    int to_read = remaining > static_cast<int>(sizeof(buffer)) ? sizeof(buffer)
                                                               : remaining;
    int received =
        httpd_req_recv(req, reinterpret_cast<char*>(buffer), to_read);
    if (received <= 0) {
      const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
      if ((received == 0 || errno == EAGAIN || errno == EWOULDBLOCK ||
           errno == EINTR) &&
          now_ms - last_progress_ms < upload_stall_budget_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      return abort_upload("500 Internal Server Error", "Upload receive failed");
    }
    last_progress_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    remaining -= received;

    if (!write_ota_chunk(buffer, static_cast<size_t>(received))) {
      if (write_error == 2) {
        return abort_upload("500 Internal Server Error",
                            "esp_ota_write failed");
      }
      return abort_upload("400 Bad Request",
                          "Upload must contain raw ESP firmware bytes");
    }
  }

  esp_err_t end_result = esp_ota_end(upload_handle_);
  if (end_result != ESP_OK) {
    std::string error_msg = "esp_ota_end failed: ";
    error_msg += esp_err_to_name(end_result);
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error",
                                 "upgrade_upload", error_msg);
    return ESP_OK;
  }

  esp_err_t partition_result = esp_ota_set_boot_partition(upload_partition_);
  if (partition_result != ESP_OK) {
    std::string error_msg = "esp_ota_set_boot_partition failed: ";
    error_msg += esp_err_to_name(partition_result);
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error",
                                 "upgrade_upload", error_msg);
    return ESP_OK;
  }

  logger.info("Upload completed: " + std::to_string(upload_bytes_received_) +
              " bytes");
  sendAndRestart(req, "Upgrade uploaded. Restarting...", "upgrade_upload");
  return ESP_OK;
}

void UpgradeManager::fetchStatus(const ebus::JsonChunkVisitor& visitor) {
  ebus::detail::JsonWriter writer(visitor);
  auto root = writer.objectScope();
  writer.writeField("ready", true);
  writer.writeField("upgrading", false);
}

esp_err_t UpgradeManager::handleStatus(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  DeviceStatus::fetchStatus([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

bool UpgradeManager::performHttpUpgrade(const std::string& url,
                                        std::string& error) {
  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  if (partition == nullptr) {
    error = "No OTA partition available";
    return false;
  }

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.timeout_ms = 20000;
  config.user_agent = "esp-ebus-upgrader/1.0";

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    error = "esp_http_client_init failed";
    return false;
  }

  esp_err_t open_result = esp_http_client_open(client, 0);
  if (open_result != ESP_OK) {
    error = std::string("esp_http_client_open failed: ") +
            esp_err_to_name(open_result);
    esp_http_client_cleanup(client);
    return false;
  }

  int header_ret = esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);
  int content_length = esp_http_client_get_content_length(client);
  bool is_chunked = esp_http_client_is_chunked_response(client);
  logger.debug("Upgrade HTTP status=" + std::to_string(status_code) +
               " headers=" + std::to_string(header_ret) +
               " content_length=" + std::to_string(content_length) +
               " chunked=" + std::to_string(is_chunked ? 1 : 0));
  if (status_code != 200) {
    error =
        std::string("Unexpected HTTP status: ") + std::to_string(status_code);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  esp_ota_handle_t handle = 0;
  esp_err_t begin_result = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &handle);
  if (begin_result != ESP_OK) {
    error =
        std::string("esp_ota_begin failed: ") + esp_err_to_name(begin_result);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  uint8_t buffer[ota_buffer_size];
  bool ok = true;
  size_t total_written = 0;
  bool checked_magic = false;
  int next_progress_percent = 10;
  size_t next_progress_bytes = progress_step_bytes;

  logger.info("HTTP upgrade download started: url=" + url +
              ", content_length=" + std::to_string(content_length) +
              ", chunked=" + std::to_string(is_chunked ? 1 : 0));

  while (true) {
    int bytes_read = esp_http_client_read(
        client, reinterpret_cast<char*>(buffer), sizeof(buffer));
    if (bytes_read < 0) {
      error = "esp_http_client_read failed";
      ok = false;
      break;
    }
    if (bytes_read == 0) {
      break;
    }

    if (!checked_magic) {
      checked_magic = true;
      if (buffer[0] != esp_image_magic) {
        char hex[4];
        snprintf(hex, sizeof(hex), "%02x", buffer[0]);
        error = "Downloaded file is not an ESP firmware image (magic=0x";
        error += hex;
        error += ")";
        ok = false;
        break;
      }
    }

    esp_err_t write_result = esp_ota_write(handle, buffer, bytes_read);
    if (write_result != ESP_OK) {
      error =
          std::string("esp_ota_write failed: ") + esp_err_to_name(write_result);
      ok = false;
      break;
    }
    total_written += static_cast<size_t>(bytes_read);
    if (content_length > 0) {
      int percent = static_cast<int>((total_written * 100) / content_length);
      if (percent >= next_progress_percent) {
        logger.info("HTTP upgrade progress " + std::to_string(percent) + "% (" +
                    std::to_string(total_written) + "/" +
                    std::to_string(content_length) + " bytes)");
        while (percent >= next_progress_percent &&
               next_progress_percent < 100) {
          next_progress_percent += 10;
        }
      }
    } else if (total_written >= next_progress_bytes) {
      logger.info("HTTP upgrade progress " + std::to_string(total_written) +
                  " bytes");
      while (total_written >= next_progress_bytes) {
        next_progress_bytes += progress_step_bytes;
      }
    }
    vTaskDelay(1);
  }

  if (ok && content_length > 0 &&
      static_cast<int>(total_written) != content_length) {
    error = std::string("Downloaded size mismatch: got ") +
            std::to_string(total_written) + ", expected " +
            std::to_string(content_length);
    ok = false;
  }
  if (ok && !esp_http_client_is_complete_data_received(client)) {
    error = "HTTP download incomplete";
    ok = false;
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (!ok) {
    esp_ota_abort(handle);
    return false;
  }

  if (total_written == 0) {
    esp_ota_abort(handle);
    error = "No firmware data downloaded";
    return false;
  }

  esp_err_t end_result = esp_ota_end(handle);
  if (end_result != ESP_OK) {
    error = std::string("esp_ota_end failed: ") + esp_err_to_name(end_result);
    return false;
  }

  esp_err_t partition_result = esp_ota_set_boot_partition(partition);
  if (partition_result != ESP_OK) {
    error = std::string("esp_ota_set_boot_partition failed: ") +
            esp_err_to_name(partition_result);
    return false;
  }

  logger.info("HTTP upgrade download completed: " +
              std::to_string(total_written) + " bytes");
  return true;
}

esp_err_t UpgradeManager::handleHttpUpgrade(httpd_req_t* req) {
  pre_upgrade_done_ = false;
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "upgrade_http",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();
  ebus::detail::JsonReader reader(sr.jsonReader().remaining());

  std::string url;
  // findKey matches current-level keys: consume the opening brace first
  // (a fresh reader positions depth before it and never matches).
  if (reader.next() == ebus::detail::JsonReader::Token::object_start &&
      reader.findKey("url")) {
    reader.next();
    url = std::string(reader.value());
  }

  if (url.empty()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "upgrade_http",
                                 "Missing 'url'");
    return ESP_OK;
  }

  prepareForUpgrade();

  std::string error;
  if (!performHttpUpgrade(url, error)) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error",
                                 "upgrade_http", error);
    return ESP_OK;
  }

  sendAndRestart(req, "Upgrade fetched. Restarting...", "upgrade_http");
  return ESP_OK;
}

void UpgradeManager::sendAndRestart(httpd_req_t* req, const char* message,
                                    const char* id) {
  HttpUtils::sendSuccessResponse(req, id, "successful", message);

  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
}
