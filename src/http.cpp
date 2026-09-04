#include "http.hpp"

#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>
#include <string>
#include <vector>

#include "adc.hpp"
#include "command_manager.hpp"
#include "config_manager.hpp"
#include "cron.hpp"
#include "ebus_accessor.hpp"
#include "http_utils.hpp"
#include "logger.hpp"
#include "main.hpp"
#include "mqtt.hpp"
#include "mqtt_ha.hpp"
#include "wifi_network_manager.hpp"

static httpd_handle_t configServer = nullptr;
static bool fallbackHandlersRegistered = false;

namespace {
// cppcheck-suppress syntaxError
extern const char common_css_start[] asm("_binary_common_css_start");
// cppcheck-suppress syntaxError
extern const char common_js_start[] asm("_binary_common_js_start");

// cppcheck-suppress syntaxError
extern const char root_html_start[] asm("_binary_root_html_start");
// cppcheck-suppress syntaxError
extern const char status_html_start[] asm("_binary_status_html_start");
// cppcheck-suppress syntaxError
extern const char adc_html_start[] asm("_binary_adc_html_start");
// cppcheck-suppress syntaxError
extern const char config_html_start[] asm("_binary_config_html_start");
// cppcheck-suppress syntaxError
extern const char upgrade_html_start[] asm("_binary_upgrade_html_start");
// cppcheck-suppress syntaxError
extern const char commands_html_start[] asm("_binary_commands_html_start");
// cppcheck-suppress syntaxError
extern const char cron_html_start[] asm("_binary_cron_html_start");
// cppcheck-suppress syntaxError
extern const char values_html_start[] asm("_binary_values_html_start");
// cppcheck-suppress syntaxError
extern const char devices_html_start[] asm("_binary_devices_html_start");
// cppcheck-suppress syntaxError
extern const char metrics_html_start[] asm("_binary_metrics_html_start");
// cppcheck-suppress syntaxError
extern const char logs_html_start[] asm("_binary_logs_html_start");

#if defined(EBUS_INTERNAL)
// Helper to prepare a JsonReader for iterating over an array of commands/keys.
// Handles both bare arrays and objects with a specific array key (e.g.,
// {"commands": [...]}). On success, the reader is positioned at the start of
// the array. On failure, returns false and populates error_out.
static bool prepareJsonReaderForArray(ebus::detail::JsonReader& reader,
                                      std::string_view expected_array_key,
                                      std::string& error_out) {
  auto token = reader.next();  // Read the first token

  if (token == ebus::detail::JsonReader::Token::object_start) {
    // If the root is an object, try to find the expected array key
    if (reader.findKey(expected_array_key)) {
      token = reader.next();  // Advance to the value of the key
    } else {
      // If the key is not found, and we expected one, it's an error
      error_out = "JSON object must contain a '" +
                  std::string(expected_array_key) + "' key.";
      return false;
    }  // If the root is a direct array, and we expected a key, that's fine.
  } else if (token != ebus::detail::JsonReader::Token::array_start) {
    // If it's neither an object nor a direct array, it's an error.
    error_out = "JSON root must be an object or a direct array.";
    return false;
  }

  if (token != ebus::detail::JsonReader::Token::array_start) {
    error_out = "Expected a JSON array.";
    return false;
  }
  return true;
}
#endif

void sendStatic(httpd_req_t* req, const char* contentType, const char* data) {
  // HttpUtils::sendResponse(req, "200 OK", contentType, std::string(data));
  HttpUtils::sendResponse(req, "200 OK", contentType, data);
}

uint32_t parseAdcArg(httpd_req_t* req, const char* key, uint32_t fallback) {
  if (req == nullptr || key == nullptr) return fallback;

  char query[256] = {};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    return fallback;

  char value[32] = {};
  if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK)
    return fallback;

  char* end = nullptr;
  unsigned long parsed = std::strtoul(value, &end, 10);
  if (end == value || *end != '\0') return fallback;
  return static_cast<uint32_t>(parsed);
}

uint32_t parseAdcChannelMask(httpd_req_t* req) {
  if (req == nullptr) return 0x03;  // default GPIO0, GPIO1

  char query[256] = {};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    return 0x03;

  char value[128] = {0};
  if (httpd_query_key_value(query, "channels", value, sizeof(value)) != ESP_OK)
    return 0x03;

  uint32_t mask = 0;
  const char* p = value;
  while (*p != '\0') {
    char* end = nullptr;
    long ch = std::strtol(p, &end, 10);
    if (end == p) break;
    if (ch >= 0 && ch <= 4) mask |= (1U << ch);
    if (*end == ',')
      p = end + 1;
    else
      break;
  }
  return mask == 0 ? 0x03 : mask;
}

esp_err_t handleCommonCss(httpd_req_t* req) {
  sendStatic(req, "text/css", common_css_start);
  return ESP_OK;
}

esp_err_t handleCommonJs(httpd_req_t* req) {
  sendStatic(req, "application/javascript", common_js_start);
  return ESP_OK;
}

esp_err_t handleRoot(httpd_req_t* req) {
  sendStatic(req, "text/html", root_html_start);
  return ESP_OK;
}

esp_err_t handleConfigPage(httpd_req_t* req) {
  sendStatic(req, "text/html", config_html_start);
  return ESP_OK;
}

esp_err_t handleWifiScan(httpd_req_t* req) {
  wifi_scan_config_t scanConfig = {};
  scanConfig.show_hidden = true;
  scanConfig.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scanConfig.scan_time.active.min = 0;
  scanConfig.scan_time.active.max = 0;
  scanConfig.scan_time.passive = 100;

  esp_err_t err = esp_wifi_scan_start(&scanConfig, true);
  if (err != ESP_OK) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "wifi_scan",
                                 "WiFi scan failed");
    return ESP_OK;
  }

  uint16_t apCount = 0;
  esp_wifi_scan_get_ap_num(&apCount);

  // Cap the number of records we read to avoid heap-allocating for every AP.
  // ESP32-C3 scans rarely see more than ~30 visible APs; 64 gives headroom
  // without a heap allocation (replaces std::vector<wifi_ap_record_t>).
  static constexpr size_t max_scan_aps = 64;
  size_t scan_count = std::min(static_cast<uint16_t>(max_scan_aps), apCount);
  static std::array<wifi_ap_record_t, max_scan_aps> aps{};
  apCount = static_cast<uint16_t>(scan_count);
  esp_wifi_scan_get_ap_records(&apCount, aps.data());

  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  ebus::detail::JsonWriter writer([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });

  {
    auto array_scope = writer.arrayScope();
    for (size_t i = 0; i < scan_count; ++i) {
      const auto& ap = aps[i];
      auto obj_scope = writer.objectScope();
      char ssid_buf[33];
      size_t ssid_len = strnlen(reinterpret_cast<const char*>(ap.ssid), 32);
      memcpy(ssid_buf, ap.ssid, ssid_len);
      ssid_buf[ssid_len] = '\0';
      writer.writeField("ssid", ssid_buf);
      char bssidStr[18];
      snprintf(bssidStr, sizeof(bssidStr), "%02x:%02x:%02x:%02x:%02x:%02x",
               ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4],
               ap.bssid[5]);
      writer.writeField("bssid", bssidStr);
      writer.writeField("rssi", ap.rssi);
      writer.writeField("channel", ap.primary);

      const char* authMode = "UNKNOWN";
      switch (ap.authmode) {
        case WIFI_AUTH_OPEN:
          authMode = "OPEN";
          break;
        case WIFI_AUTH_WEP:
          authMode = "WEP";
          break;
        case WIFI_AUTH_WPA_PSK:
          authMode = "WPA_PSK";
          break;
        case WIFI_AUTH_WPA2_PSK:
          authMode = "WPA2_PSK";
          break;
        case WIFI_AUTH_WPA_WPA2_PSK:
          authMode = "WPA_WPA2_PSK";
          break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
          authMode = "WPA2_ENTERPRISE";
          break;
        case WIFI_AUTH_WPA3_PSK:
          authMode = "WPA3_PSK";
          break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
          authMode = "WPA2_WPA3_PSK";
          break;
        default:
          break;
      }
      writer.writeField("authMode", authMode);
    }
  }
  httpd_resp_send_chunk(req, nullptr, 0);
  esp_wifi_clear_ap_list();
  return ESP_OK;
}

esp_err_t handleUpgradePage(httpd_req_t* req) {
  sendStatic(req, "text/html", upgrade_html_start);
  return ESP_OK;
}

esp_err_t handleStatusPage(httpd_req_t* req) {
  sendStatic(req, "text/html", status_html_start);
  return ESP_OK;
}

esp_err_t handleStatus(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  fetchStatus([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

#if defined(EBUS_INTERNAL)
esp_err_t handleStatusApp(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  fetchAppStatus([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleStatusLib(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  getEbusController().fetchStatus([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}
#endif

esp_err_t handleAdcPage(httpd_req_t* req) {
  sendStatic(req, "text/html", adc_html_start);
  return ESP_OK;
}

esp_err_t handleAdcRaw(httpd_req_t* req) {
  if (!adc.isRunning() && !adc.begin()) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "adc_raw",
                                 "ADC not running or failed to start");
    return ESP_OK;
  }

  const uint32_t sampleRate = parseAdcArg(req, "sample_rate", 30000);
  const uint32_t samplesPerChannel = parseAdcArg(
      req, "samples_per_channel", parseAdcArg(req, "sample_count", 2400));
  const uint32_t channelMask = parseAdcChannelMask(req);
  const uint32_t effectivePerChannelRate =
      adc.effectivePerChannelSampleRate(sampleRate, channelMask);
  const uint32_t activeChannelCount =
      static_cast<uint32_t>(__builtin_popcount(channelMask & 0x1F));
  const uint32_t controllerRate =
      effectivePerChannelRate *
      (activeChannelCount == 0 ? 1U : activeChannelCount);

  const uint64_t captureStartMillis =
      static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);

  char tmp1[32], tmp2[32], tmp3[32], tmp4[32], tmp5[32], tmp6[32];
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "X-ADC-Format", "esp32c3-ch12c3-le16");
  HttpUtils::applyCustomHeaders(req);

  std::snprintf(tmp1, sizeof(tmp1), "%u",
                static_cast<unsigned>(effectivePerChannelRate));
  httpd_resp_set_hdr(req, "X-ADC-Sample-Rate", tmp1);

  std::snprintf(tmp2, sizeof(tmp2), "%u",
                static_cast<unsigned>(samplesPerChannel));
  httpd_resp_set_hdr(req, "X-ADC-Samples", tmp2);

  std::snprintf(tmp3, sizeof(tmp3), "%u", static_cast<unsigned>(channelMask));
  httpd_resp_set_hdr(req, "X-ADC-Channel-Mask", tmp3);

  std::snprintf(tmp4, sizeof(tmp4), "%u",
                static_cast<unsigned>(Adc::result_bytes));
  httpd_resp_set_hdr(req, "X-ADC-Result-Bytes", tmp4);
  std::snprintf(tmp5, sizeof(tmp5), "%llu",
                static_cast<unsigned long long>(captureStartMillis));
  httpd_resp_set_hdr(req, "X-ADC-Capture-Start-Millis", tmp5);
  std::snprintf(tmp6, sizeof(tmp6), "%u",
                static_cast<unsigned>(controllerRate));
  httpd_resp_set_hdr(req, "X-ADC-Controller-Sample-Rate", tmp6);

  if (!adc.streamRaw(
          [req](std::string_view chunk) {
            httpd_resp_send_chunk(req, chunk.data(), chunk.size());
          },
          sampleRate, samplesPerChannel, channelMask))
    return ESP_FAIL;
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleAdcState(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  {
    ebus::detail::JsonWriter writer([req](std::string_view chunk) {
      httpd_resp_send_chunk(req, chunk.data(), chunk.size());
    });
    auto scope = writer.objectScope();
    writer.writeField("running", adc.isRunning());
  }
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleAdcEnable(httpd_req_t* req) {
  if (adc.begin()) {
    HttpUtils::sendSuccessResponse(req, "adc_enable");
  } else {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "adc_enable",
                                 "ADC enable failed");
  }
  return ESP_OK;
}

esp_err_t handleAdcDisable(httpd_req_t* req) {
  adc.stop();
  HttpUtils::sendSuccessResponse(req, "adc_disable");
  return ESP_OK;
}

#if defined(EBUS_INTERNAL)
esp_err_t handleCommandsPage(httpd_req_t* req) {
  sendStatic(req, "text/html", commands_html_start);
  return ESP_OK;
}

esp_err_t handleCommands(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  commandManager.fetchCommands([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleCommandsEvaluate(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();

  ebus::detail::JsonReader& reader = sr.jsonReader();
  std::string parse_error;
  if (!prepareJsonReaderForArray(reader, "commands", parse_error)) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate",
                                 parse_error);
    return ESP_OK;
  }

  std::string_view evalError;
  bool headerSeen = false;
  while (true) {
    std::string_view row_sv = reader.rawValue();
    if (row_sv.empty()) break;
    ebus::detail::JsonReader row_reader(row_sv);
    auto row_token = row_reader.next();

    if (row_token == ebus::detail::JsonReader::Token::array_start) {
      if (!headerSeen) {
        headerSeen = true;
        continue;
      }
    } else if (row_token == ebus::detail::JsonReader::Token::object_start) {
      row_reader.reset();
      evalError = Command::evaluate(row_reader);
      if (!evalError.empty()) break;
    }
  }

  if (!evalError.empty())
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate", evalError);
  else
    HttpUtils::sendSuccessResponse(req, "evaluate");

  return ESP_OK;
}

esp_err_t handleCommandsInsert(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "insert",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();

  std::string_view body_sv = sr.jsonReader().remaining();

  ebus::detail::JsonReader reader_eval(body_sv);
  std::string parse_error;
  if (!prepareJsonReaderForArray(reader_eval, "commands", parse_error)) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "insert", parse_error);
    return ESP_OK;
  }

  std::string_view evalError;
  bool headerSeen = false;
  while (true) {
    std::string_view cmd_sv = reader_eval.rawValue();
    if (cmd_sv.empty()) break;
    ebus::detail::JsonReader row_reader(cmd_sv);
    auto row_token = row_reader.next();
    if (row_token == ebus::detail::JsonReader::Token::array_start) {
      if (!headerSeen) {
        headerSeen = true;
        continue;
      }
    } else if (row_token == ebus::detail::JsonReader::Token::object_start) {
      row_reader.reset();
      evalError = Command::evaluate(row_reader);
      if (!evalError.empty()) break;
    }
  }

  if (!evalError.empty()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "insert", evalError);
    return ESP_OK;
  }

  ebus::detail::JsonReader reader_insert(body_sv);
  prepareJsonReaderForArray(reader_insert, "commands", parse_error);
  headerSeen = false;
  while (true) {
    std::string_view cmd_sv = reader_insert.rawValue();
    if (cmd_sv.empty()) break;
    ebus::detail::JsonReader row_reader(cmd_sv);
    auto row_token = row_reader.next();
    if (row_token == ebus::detail::JsonReader::Token::array_start) {
      if (!headerSeen) {
        headerSeen = true;
        continue;
      }
      row_reader.reset();
      commandManager.insertCommand(Command::fromTabular(row_reader));
    } else if (row_token == ebus::detail::JsonReader::Token::object_start) {
      row_reader.reset();
      commandManager.insertCommand(Command::fromJson(row_reader));
    }
  }
  Mqtt::publishComponentDiscovery();
  HttpUtils::sendSuccessResponse(req, "insert");
  return ESP_OK;
}

esp_err_t handleCommandsUpload(httpd_req_t* req) {
  if (req->method != HTTP_POST) {
    HttpUtils::sendErrorResponse(req, "405 Method Not Allowed", "upload",
                                 "POST required");
    return ESP_OK;
  }

  if (!commandManager.initFileSystem()) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "LittleFS init failed");
    return ESP_OK;
  }

  const char* tmp_path = "/littlefs/commands.json.tmp";

  FILE* file = std::fopen(tmp_path, "wb");
  if (file == nullptr) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "Failed to open temp file");
    return ESP_OK;
  }

  char buffer[512];
  int remaining = req->content_len;
  int total_written = 0;

  while (remaining > 0) {
    int to_read = remaining > static_cast<int>(sizeof(buffer))
                      ? static_cast<int>(sizeof(buffer))
                      : remaining;
    int received = httpd_req_recv(req, buffer, to_read);
    if (received <= 0) {
      std::fclose(file);
      std::remove(tmp_path);
      HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                   "Receive failed");
      return ESP_OK;
    }
    int written = std::fwrite(buffer, 1, received, file);
    if (written != received) {
      std::fclose(file);
      std::remove(tmp_path);
      HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                   "Write failed");
      return ESP_OK;
    }
    total_written += written;
    remaining -= received;
  }

  std::fclose(file);

  int64_t bytes = commandManager.loadCommandsFrom(tmp_path);
  if (bytes < 0) {
    std::remove(tmp_path);
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "JSON parse failed");
    return ESP_OK;
  }

  if (commandManager.saveCommands() < 0) {
    std::remove(tmp_path);
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "upload",
                                 "Save failed");
    return ESP_OK;
  }

  std::remove(tmp_path);
  Mqtt::publishComponentDiscovery();
  size_t count = commandManager.getCommandCount();
  char res_buf[128];
  snprintf(res_buf, sizeof(res_buf), "Uploaded %d bytes, loaded %u commands",
           total_written, (unsigned)count);
  HttpUtils::sendSuccessResponse(req, "upload", res_buf);
  return ESP_OK;
}

esp_err_t handleCommandsRemove(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "remove",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();

  ebus::detail::JsonReader reader(sr.jsonReader().remaining());
  std::string parse_error;
  if (prepareJsonReaderForArray(reader, "keys", parse_error)) {
    while (true) {
      auto t = reader.next();
      if (t == ebus::detail::JsonReader::Token::array_end ||
          t == ebus::detail::JsonReader::Token::end)
        break;
      if (t == ebus::detail::JsonReader::Token::string)
        commandManager.removeCommand(reader.value());
    }
  } else {
    commandManager.removeAll();
  }
  HttpUtils::sendSuccessResponse(req, "remove");
  return ESP_OK;
}

esp_err_t handleCommandsLoad(httpd_req_t* req) {
  int64_t bytes = commandManager.loadCommands();
  if (bytes > 0) {
    Mqtt::publishComponentDiscovery();
    HttpUtils::sendSuccessResponse(
        req, "load", "successful",
        "Loaded " + std::to_string(bytes) + " bytes");
  } else if (bytes < 0) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "load",
                                 "Load failed");
  } else {
    HttpUtils::sendSuccessResponse(req, "load", "no data");
  }
  return ESP_OK;
}

esp_err_t handleCommandsSave(httpd_req_t* req) {
  int64_t bytes = commandManager.saveCommands();
  if (bytes > 0) {
    Mqtt::publishComponentDiscovery();
    HttpUtils::sendSuccessResponse(req, "save", "successful",
                                   "Saved " + std::to_string(bytes) + " bytes");
  } else if (bytes < 0) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "save",
                                 "Save failed");
  } else {
    HttpUtils::sendSuccessResponse(req, "save", "no data");
  }
  return ESP_OK;
}

esp_err_t handleCommandsWipe(httpd_req_t* req) {
  if (mqttha.isEnabled()) {
    mqttha.removeComponents();
  }
  int64_t bytes = commandManager.wipeCommands();
  if (bytes > 0) {
    HttpUtils::sendSuccessResponse(req, "wipe", "successful",
                                   "Wiped " + std::to_string(bytes) + " bytes");
  } else if (bytes < 0) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "wipe",
                                 "Wipe failed");
  } else {
    HttpUtils::sendSuccessResponse(req, "wipe", "no data");
  }
  return ESP_OK;
}

esp_err_t handleCronPage(httpd_req_t* req) {
  sendStatic(req, "text/html", cron_html_start);
  return ESP_OK;
}

esp_err_t handleCron(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  cron.fetchRules([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleCronEvaluate(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();

  ebus::detail::JsonReader reader(sr.jsonReader().remaining());
  std::string parse_error;
  if (!prepareJsonReaderForArray(reader, "", parse_error)) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate",
                                 parse_error);
    return ESP_OK;
  }

  std::string evalError;
  while (true) {
    std::string_view rule_sv = reader.rawValue();
    if (rule_sv.empty()) break;
    ebus::detail::JsonReader rule_reader(rule_sv);
    evalError = Cron::evaluate(rule_reader);
    if (!evalError.empty()) break;
  }

  if (!evalError.empty())
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "evaluate", evalError);
  else
    HttpUtils::sendSuccessResponse(req, "evaluate");

  return ESP_OK;
}

esp_err_t handleCronSave(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "save",
                                 "Receive failed");
    return ESP_OK;
  }
  sr.endOfInput();
  int64_t bytes = cron.replaceRules(sr.jsonReader().remaining());
  if (bytes >= 0) {
    HttpUtils::sendSuccessResponse(
        req, "save", "successful",
        bytes > 0 ? "Saved " + std::to_string(bytes) + " bytes" : "");
  } else {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "save",
                                 "Save failed");
  }
  return ESP_OK;
}

esp_err_t handleCronLoad(httpd_req_t* req) {
  int64_t bytes = cron.loadRules();
  if (bytes > 0) {
    HttpUtils::sendSuccessResponse(
        req, "load", "successful",
        "Loaded " + std::to_string(bytes) + " bytes");
  } else if (bytes < 0) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "load",
                                 "Load failed");
  } else {
    HttpUtils::sendSuccessResponse(req, "load", "no data");
  }
  return ESP_OK;
}

esp_err_t handleValuesPage(httpd_req_t* req) {
  sendStatic(req, "text/html", values_html_start);
  return ESP_OK;
}

esp_err_t handleValues(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  commandManager.fetchValues([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleValuesWrite(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "write",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();

  std::string_view body_sv = sr.jsonReader().remaining();
  ebus::detail::JsonReader reader(body_sv);
  std::string key;
  if (reader.findKey("key") &&
      reader.next() == ebus::detail::JsonReader::Token::string) {
    key = std::string(reader.value());
  }

  Command* command = commandManager.findCommand(key);
  if (command == nullptr) {
    HttpUtils::sendErrorResponse(req, "404 Not Found", "write",
                                 "Key '" + key + "' not found");
    return ESP_OK;
  }

  ebus::Sequence valueBytes = command->getVectorFromJson(body_sv);
  if (!valueBytes.empty()) {
    ebus::Sequence fullWrite =
        ebus::makeSequence(command->getWriteCmd(commandManager));
    fullWrite.append(valueBytes);
    getEbusController().enqueue(prio_send, fullWrite);
    command->setLast(0);
    HttpUtils::sendSuccessResponse(req, "write");
  } else {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "write",
                                 "Invalid value for key '" + key + "'");
  }
  return ESP_OK;
}

esp_err_t handleValuesRead(httpd_req_t* req) {
  HttpUtils::StreamingReader sr(req);
  if (!sr.isValid() || !sr.feedAll()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "read",
                                 "Request body too large or invalid");
    return ESP_OK;
  }
  sr.endOfInput();

  ebus::detail::JsonReader reader(sr.jsonReader().remaining());
  std::string key;
  if (reader.findKey("key") &&
      reader.next() == ebus::detail::JsonReader::Token::string) {
    key = std::string(reader.value());
  }

  if (key.empty()) {
    HttpUtils::sendErrorResponse(req, "400 Bad Request", "read",
                                 "invalid json payload");
    return ESP_OK;
  }

  Command* command = commandManager.findCommand(key);
  if (command != nullptr) {
    command->setLast(0);
    HttpUtils::sendSuccessResponse(req, "read", "requested");
  } else {
    HttpUtils::sendErrorResponse(req, "404 Not Found", "read",
                                 "Key '" + key + "' not found");
  }
  return ESP_OK;
}

esp_err_t handleDevicesPage(httpd_req_t* req) {
  sendStatic(req, "text/html", devices_html_start);
  return ESP_OK;
}

esp_err_t handleDevices(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  getEbusController().fetchDevices([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleDevicesScan(httpd_req_t* req) {
  getEbusController().scanObservedDevices();
  HttpUtils::sendSuccessResponse(req, "scan", "initiated");
  return ESP_OK;
}

esp_err_t handleDevicesScanFull(httpd_req_t* req) {
  getEbusController().initFullScan(true);
  HttpUtils::sendSuccessResponse(req, "scan_full", "initiated");
  return ESP_OK;
}

esp_err_t handleMetricsPage(httpd_req_t* req) {
  sendStatic(req, "text/html", metrics_html_start);
  return ESP_OK;
}

esp_err_t handleMetrics(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  getEbusController().fetchMetrics([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleMetricsReset(httpd_req_t* req) {
  getEbusController().resetMetrics();
  HttpUtils::sendSuccessResponse(req, "reset");
  return ESP_OK;
}

esp_err_t handleLogsPage(httpd_req_t* req) {
  sendStatic(req, "text/html", logs_html_start);
  return ESP_OK;
}

esp_err_t handleLogs(httpd_req_t* req) {
  uint64_t sinceMillis = 0;
  const size_t queryLen = httpd_req_get_url_query_len(req);
  if (queryLen > 0) {
    char queryBuf[256];
    if (queryLen + 1 > sizeof(queryBuf)) {
      httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, nullptr);
      return ESP_OK;
    }
    if (httpd_req_get_url_query_str(req, queryBuf, sizeof(queryBuf)) ==
        ESP_OK) {
      char sinceBuffer[32] = {0};
      if (httpd_query_key_value(queryBuf, "since", sinceBuffer,
                                sizeof(sinceBuffer)) == ESP_OK) {
        sinceMillis = std::strtoull(sinceBuffer, nullptr, 10);
      }
    }
  }
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  logger.fetchLogs(
      [req](std::string_view chunk) {
        httpd_resp_send_chunk(req, chunk.data(), chunk.size());
      },
      sinceMillis);
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleLogsTimeRelation(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  logger.fetchTimeRelation([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}
#endif

esp_err_t handleRestart(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", "Restarting...");
  vTaskDelay(pdMS_TO_TICKS(500));
  restart();
  return ESP_OK;
}

esp_err_t handleNotFound(httpd_req_t* req) {
  if (!WifiNetworkManager::isStaConnected() &&
      WifiNetworkManager::getMode() != WIFI_MODE_STA) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Location", "/config");
    HttpUtils::applyCustomHeaders(req);
    httpd_resp_send(req, "", 0);
    return ESP_OK;
  }

  HttpUtils::sendResponse(req, "404 Not Found", "text/plain", "Not found");
  return ESP_OK;
}
}  // namespace

httpd_handle_t GetHttpServer() { return configServer; }

bool RegisterUri(const char* uri, httpd_method_t method,
                 esp_err_t (*handler)(httpd_req_t*)) {
  if (configServer == nullptr) {
    logger.error(std::string("HTTP server not started; cannot register ") +
                 uri);
    return false;
  }
  return HttpUtils::registerRoute(configServer, uri, method, handler);
}

void SetupHttpHandlers() {
  if (configServer != nullptr) return;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 64;
  config.stack_size = 8192;
  config.lru_purge_enable = true;
  config.max_open_sockets = 2;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;

  if (httpd_start(&configServer, &config) != ESP_OK) {
    logger.error("Failed to start HTTP server");
    return;
  }

  RegisterUri("/common.css", HTTP_GET, handleCommonCss);
  RegisterUri("/common.js", HTTP_GET, handleCommonJs);
  RegisterUri("/", HTTP_GET, handleRoot);
  RegisterUri("/config", HTTP_GET, handleConfigPage);
  RegisterUri("/upgrade", HTTP_GET, handleUpgradePage);

  RegisterUri("/status", HTTP_GET, handleStatusPage);
  RegisterUri("/api/v1/status", HTTP_GET, handleStatus);
#if defined(EBUS_INTERNAL)
  RegisterUri("/api/v1/status/app", HTTP_GET, handleStatusApp);
  RegisterUri("/api/v1/status/lib", HTTP_GET, handleStatusLib);
#endif

  RegisterUri("/adc", HTTP_GET, handleAdcPage);
  RegisterUri("/api/v1/adc/raw", HTTP_GET, handleAdcRaw);
  RegisterUri("/api/v1/adc/enable", HTTP_POST, handleAdcEnable);
  RegisterUri("/api/v1/adc/disable", HTTP_POST, handleAdcDisable);
  RegisterUri("/api/v1/adc/state", HTTP_GET, handleAdcState);
  RegisterUri("/api/v1/wifi/scan", HTTP_POST, handleWifiScan);

#if defined(EBUS_INTERNAL)
  RegisterUri("/commands", HTTP_GET, handleCommandsPage);
  RegisterUri("/api/v1/commands", HTTP_GET, handleCommands);
  RegisterUri("/api/v1/commands/evaluate", HTTP_POST, handleCommandsEvaluate);
  RegisterUri("/api/v1/commands/insert", HTTP_POST, handleCommandsInsert);
  RegisterUri("/api/v1/commands/upload", HTTP_POST, handleCommandsUpload);
  RegisterUri("/api/v1/commands/remove", HTTP_POST, handleCommandsRemove);
  RegisterUri("/api/v1/commands/load", HTTP_POST, handleCommandsLoad);
  RegisterUri("/api/v1/commands/save", HTTP_POST, handleCommandsSave);
  RegisterUri("/api/v1/commands/wipe", HTTP_POST, handleCommandsWipe);

  RegisterUri("/cron", HTTP_GET, handleCronPage);
  RegisterUri("/api/v1/cron", HTTP_GET, handleCron);
  RegisterUri("/api/v1/cron", HTTP_POST, handleCronSave);
  RegisterUri("/api/v1/cron/load", HTTP_POST, handleCronLoad);
  RegisterUri("/api/v1/cron/evaluate", HTTP_POST, handleCronEvaluate);

  RegisterUri("/values", HTTP_GET, handleValuesPage);
  RegisterUri("/api/v1/values", HTTP_GET, handleValues);
  RegisterUri("/api/v1/values/write", HTTP_POST, handleValuesWrite);
  RegisterUri("/api/v1/values/read", HTTP_POST, handleValuesRead);

  RegisterUri("/devices", HTTP_GET, handleDevicesPage);
  RegisterUri("/api/v1/devices", HTTP_GET, handleDevices);
  RegisterUri("/api/v1/devices/scan", HTTP_POST, handleDevicesScan);
  RegisterUri("/api/v1/devices/scan/full", HTTP_POST, handleDevicesScanFull);

  RegisterUri("/metrics", HTTP_GET, handleMetricsPage);
  RegisterUri("/api/v1/metrics", HTTP_GET, handleMetrics);
  RegisterUri("/api/v1/metrics/reset", HTTP_POST, handleMetricsReset);

  RegisterUri("/logs", HTTP_GET, handleLogsPage);
  RegisterUri("/api/v1/logs", HTTP_GET, handleLogs);
  RegisterUri("/api/v1/logs/time-relation", HTTP_GET, handleLogsTimeRelation);
#endif

  RegisterUri("/restart", HTTP_GET, handleRestart);
}  // namespace

void SetupHttpFallbackHandlers() {
  if (configServer == nullptr || fallbackHandlersRegistered) return;
  RegisterUri("/*", HTTP_GET, handleNotFound);
  RegisterUri("/*", HTTP_POST, handleNotFound);
  fallbackHandlersRegistered = true;
}
