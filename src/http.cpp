#include "http.hpp"

#include <cJSON.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstdlib>
#include <ebus/detail/json_writer.hpp>
#include <string>
#include <vector>

#include "Adc.hpp"
#include "ConfigManager.hpp"
#include "Cron.hpp"
#include "HttpUtils.hpp"
#include "Logger.hpp"
#include "MqttHA.hpp"
#include "Store.hpp"
#include "WifiNetworkManager.hpp"
#include "ebus_accessor.hpp"
#include "main.hpp"

static httpd_handle_t configServer = nullptr;
static bool fallbackHandlersRegistered = false;

namespace {
extern const char common_css_start[] asm("_binary_common_css_start");
extern const char common_js_start[] asm("_binary_common_js_start");

extern const char root_html_start[] asm("_binary_root_html_start");
extern const char status_html_start[] asm("_binary_status_html_start");
extern const char adc_html_start[] asm("_binary_adc_html_start");
extern const char config_html_start[] asm("_binary_config_html_start");
extern const char upgrade_html_start[] asm("_binary_upgrade_html_start");
extern const char commands_html_start[] asm("_binary_commands_html_start");
extern const char cron_html_start[] asm("_binary_cron_html_start");
extern const char values_html_start[] asm("_binary_values_html_start");
extern const char devices_html_start[] asm("_binary_devices_html_start");
extern const char metrics_html_start[] asm("_binary_metrics_html_start");
extern const char logs_html_start[] asm("_binary_logs_html_start");

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

esp_err_t handleStatusPage(httpd_req_t* req) {
  sendStatic(req, "text/html", status_html_start);
  return ESP_OK;
}

esp_err_t handleAdcPage(httpd_req_t* req) {
  sendStatic(req, "text/html", adc_html_start);
  return ESP_OK;
}

esp_err_t handleStatusApi(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  fetchStatusJson([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleAdcRaw(httpd_req_t* req) {
  if (!adc.isRunning() && !adc.begin()) {
    HttpUtils::sendResponse(req, "500 Internal Server Error",
                            "application/json;charset=utf-8",
                            "{\"error\":\"adc not running\"}");
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

  std::snprintf(tmp1, sizeof(tmp1), "%u",
                static_cast<unsigned>(effectivePerChannelRate));
  httpd_resp_set_hdr(req, "X-ADC-Sample-Rate", tmp1);

  std::snprintf(tmp2, sizeof(tmp2), "%u",
                static_cast<unsigned>(samplesPerChannel));
  httpd_resp_set_hdr(req, "X-ADC-Samples", tmp2);

  std::snprintf(tmp3, sizeof(tmp3), "%u", static_cast<unsigned>(channelMask));
  httpd_resp_set_hdr(req, "X-ADC-Channel-Mask", tmp3);

  std::snprintf(tmp4, sizeof(tmp4), "%u",
                static_cast<unsigned>(Adc::RESULT_BYTES));
  httpd_resp_set_hdr(req, "X-ADC-Result-Bytes", tmp4);
  std::snprintf(tmp5, sizeof(tmp5), "%llu",
                static_cast<unsigned long long>(captureStartMillis));
  httpd_resp_set_hdr(req, "X-ADC-Capture-Start-Millis", tmp5);
  std::snprintf(tmp6, sizeof(tmp6), "%u",
                static_cast<unsigned>(controllerRate));
  httpd_resp_set_hdr(req, "X-ADC-Controller-Sample-Rate", tmp6);

  if (!adc.streamRaw(req, sampleRate, samplesPerChannel, channelMask))
    return ESP_FAIL;
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleAdcState(httpd_req_t* req) {
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("running", adc.isRunning());
  writer.endObject();
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleAdcEnable(httpd_req_t* req) {
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("id", "adc_enable");
  const bool started = adc.begin();
  if (started) {
    writer.writeField("status", "successful");
  } else {
    writer.writeField("status", "failed");
    writer.writeField("error", "ADC enable failed");
  }
  writer.endObject();
  HttpUtils::sendResponse(req, started ? "200 OK" : "500 Internal Server Error",
                          "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleAdcDisable(httpd_req_t* req) {
  adc.stop();
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8",
                          "{\"id\":\"adc_disable\",\"status\":\"successful\"}");
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
    HttpUtils::sendResponse(req, "500 Internal Server Error",
                            "application/json;charset=utf-8",
                            "{\"id\":\"wifi_scan\",\"status\":\"failed\","
                            "\"error\":\"WiFi scan failed\"}");
    return ESP_OK;
  }

  uint16_t apCount = 0;
  esp_wifi_scan_get_ap_num(&apCount);

  std::vector<wifi_ap_record_t> aps(apCount);
  esp_wifi_scan_get_ap_records(&apCount, aps.data());

  httpd_resp_set_type(req, "application/json;charset=utf-8");
  ebus::detail::JsonWriter writer([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });

  writer.startArray();
  for (const auto& ap : aps) {
    writer.startObject();
    std::string ssid(reinterpret_cast<const char*>(ap.ssid),
                     strnlen(reinterpret_cast<const char*>(ap.ssid), 32));
    writer.writeField("ssid", ssid);
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
    writer.endObject();
  }
  writer.endArray();
  writer.flush();
  httpd_resp_send_chunk(req, nullptr, 0);
  esp_wifi_clear_ap_list();
  return ESP_OK;
}

esp_err_t handleUpgradePage(httpd_req_t* req) {
  sendStatic(req, "text/html", upgrade_html_start);
  return ESP_OK;
}

#if defined(EBUS_INTERNAL)
esp_err_t handleCommandsPage(httpd_req_t* req) {
  sendStatic(req, "text/html", commands_html_start);
  return ESP_OK;
}

esp_err_t handleCommands(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  store.fetchCommandsJson([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleCommandsEvaluate(httpd_req_t* req) {
  std::string body = HttpUtils::readBody(req);
  cJSON* doc = cJSON_Parse(body.c_str());
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("id", "evaluate");
  if (doc == nullptr || !cJSON_IsArray(doc)) {
    writer.writeField("status", "failed");
    writer.writeField("error", "JSON root must be an array");
  } else {
    cJSON* cmd_node = nullptr;
    std::string evalError;
    cJSON_ArrayForEach(cmd_node, doc) {
      evalError = Command::evaluate(cmd_node);
      if (!evalError.empty()) break;
    }
    if (evalError.empty()) {
      writer.writeField("status", "successful");
    } else {
      writer.writeField("status", "failed");
      writer.writeField("error", evalError);
    }
  }
  writer.endObject();
  if (doc) cJSON_Delete(doc);
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleCommandsInsert(httpd_req_t* req) {
  std::string body = HttpUtils::readBody(req);
  cJSON* doc = cJSON_Parse(body.c_str());
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("id", "insert");
  if (doc == nullptr || !cJSON_IsArray(doc)) {
    writer.writeField("status", "failed");
    writer.writeField("error", "JSON root must be an array");
  } else {
    cJSON* cmd_node = nullptr;
    std::string evalError;
    cJSON_ArrayForEach(cmd_node, doc) {
      evalError = Command::evaluate(cmd_node);
      if (!evalError.empty()) break;
    }
    if (evalError.empty()) {
      cJSON_ArrayForEach(cmd_node, doc) {
        store.insertCommand(Command::fromJson(cmd_node));
      }
      if (mqttha.isEnabled()) mqttha.publishComponents();
      writer.writeField("status", "successful");
    } else {
      writer.writeField("status", "failed");
      writer.writeField("error", evalError);
    }
  }
  writer.endObject();
  if (doc) cJSON_Delete(doc);
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleCommandsRemove(httpd_req_t* req) {
  std::string body = HttpUtils::readBody(req);
  std::string_view keys_part = ebus::extractSub(body, "keys");
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("id", "remove");
  if (keys_part.empty() || keys_part == "[]") {
    auto cmds = store.getCommands();
    for (const Command* cmd : cmds) store.removeCommand(cmd->getKey());
  } else {
    size_t pos = 0;
    while (pos < keys_part.size()) {
      size_t s = keys_part.find('"', pos);
      if (s == std::string_view::npos) break;
      size_t e = keys_part.find('"', s + 1);
      if (e == std::string_view::npos) break;
      store.removeCommand(std::string(keys_part.substr(s + 1, e - s - 1)));
      pos = e + 1;
    }
  }
  writer.writeField("status", "successful");
  writer.endObject();
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleCommandsLoad(httpd_req_t* req) {
  int64_t bytes = store.loadCommands();
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("id", "load");
  if (bytes > 0)
    writer.writeField("status", "successful");
  else if (bytes < 0)
    writer.writeField("status", "failed");
  else
    writer.writeField("status", "no data");
  if (bytes > 0) writer.writeField("bytes", static_cast<uint32_t>(bytes));
  writer.endObject();
  if (mqttha.isEnabled()) mqttha.publishComponents();
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleCommandsSave(httpd_req_t* req) {
  int64_t bytes = store.saveCommands();
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("id", "save");
  if (bytes > 0)
    writer.writeField("status", "successful");
  else if (bytes < 0)
    writer.writeField("status", "failed");
  else
    writer.writeField("status", "no data");
  if (bytes > 0) writer.writeField("bytes", static_cast<uint32_t>(bytes));
  writer.endObject();
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleCommandsWipe(httpd_req_t* req) {
  int64_t bytes = store.wipeCommands();
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("id", "wipe");
  if (bytes > 0)
    writer.writeField("status", "successful");
  else if (bytes < 0)
    writer.writeField("status", "failed");
  else
    writer.writeField("status", "no data");
  if (bytes > 0) writer.writeField("bytes", static_cast<uint32_t>(bytes));
  writer.endObject();
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleCronPage(httpd_req_t* req) {
  sendStatic(req, "text/html", cron_html_start);
  return ESP_OK;
}

esp_err_t handleCron(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  cron.fetchRulesJson([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleCronEvaluate(httpd_req_t* req) {
  std::string body = HttpUtils::readBody(req);
  cJSON* doc = cJSON_Parse(body.c_str());
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  {
    ebus::detail::JsonWriter writer([req](std::string_view chunk) {
      httpd_resp_send_chunk(req, chunk.data(), chunk.size());
    });
    writer.startObject();
    writer.writeField("id", "evaluate");
    if (!cJSON_IsArray(doc)) {
      writer.writeField("status", "failed");
      writer.writeField("error", "JSON root must be an array");
    } else {
      cJSON* rule = nullptr;
      std::string evalError;
      cJSON_ArrayForEach(rule, doc) {
        evalError = Cron::evaluate(rule);
        if (!evalError.empty()) break;
      }
      if (evalError.empty()) {
        writer.writeField("status", "successful");
      } else {
        writer.writeField("status", "failed");
        writer.writeField("error", evalError);
      }
    }
    writer.endObject();
  }
  if (doc) cJSON_Delete(doc);
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleCronSave(httpd_req_t* req) {
  int64_t bytes = cron.replaceRules(HttpUtils::readBody(req));
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("id", "save");
  if (bytes >= 0)
    writer.writeField("status", "successful");
  else
    writer.writeField("status", "failed");
  if (bytes > 0) writer.writeField("bytes", static_cast<uint32_t>(bytes));
  writer.endObject();
  HttpUtils::sendResponse(req,
                          bytes >= 0 ? "200 OK" : "500 Internal Server Error",
                          "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleCronLoad(httpd_req_t* req) {
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("id", "load");
  int64_t bytes = cron.loadRules();
  if (bytes > 0)
    writer.writeField("status", "successful");
  else if (bytes < 0)
    writer.writeField("status", "failed");
  else
    writer.writeField("status", "no data");
  if (bytes > 0) writer.writeField("bytes", static_cast<uint32_t>(bytes));
  writer.endObject();
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleValuesPage(httpd_req_t* req) {
  sendStatic(req, "text/html", values_html_start);
  return ESP_OK;
}

esp_err_t handleValues(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  store.fetchValuesJson([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleValuesWrite(httpd_req_t* req) {
  std::string body = HttpUtils::readBody(req);
  std::string_view key_view = ebus::extract(body, "key");
  if (key_view.size() >= 2 && key_view.front() == '"' &&
      key_view.back() == '"') {
    key_view.remove_prefix(1);
    key_view.remove_suffix(1);
  }
  std::string key(key_view);
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("id", "write");
  Command* command = store.findCommand(key);
  if (command == nullptr) {
    writer.writeField("status", "failed");
    writer.writeField("error", "Key '" + key + "' not found");
  } else {
    ebus::Sequence valueBytes = command->getVectorFromJson(body);
    if (!valueBytes.empty()) {
      ebus::Sequence fullWrite = command->getWriteCmd();
      fullWrite.append(valueBytes);
      getEbusController().enqueue(PRIO_SEND, fullWrite);
      command->setLast(0);
      writer.writeField("status", "successful");
    } else {
      writer.writeField("status", "failed");
      writer.writeField("error", "Invalid value for key '" + key + "'");
    }
  }
  writer.endObject();
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleValuesRead(httpd_req_t* req) {
  std::string body = HttpUtils::readBody(req);
  std::string_view key_view = ebus::extract(body, "key");
  if (key_view.size() >= 2 && key_view.front() == '"' &&
      key_view.back() == '"') {
    key_view.remove_prefix(1);
    key_view.remove_suffix(1);
  }
  std::string key(key_view);
  std::string out;
  ebus::detail::JsonWriter writer(
      [&out](std::string_view s) { out.append(s); });
  writer.startObject();
  writer.writeField("id", "read");
  if (key.empty()) {
    writer.writeField("status", "failed");
    writer.writeField("error", "invalid json payload");
  } else {
    Command* command = store.findCommand(key);
    if (command != nullptr) {
      command->setLast(0);
      writer.writeField("status", "requested");
    } else {
      writer.writeField("status", "failed");
      writer.writeField("error", "Key '" + key + "' not found");
    }
  }
  writer.endObject();
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8", out);
  return ESP_OK;
}

esp_err_t handleDevicesPage(httpd_req_t* req) {
  sendStatic(req, "text/html", devices_html_start);
  return ESP_OK;
}

esp_err_t handleDevices(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  ebus::detail::JsonWriter writer([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  writer.startArray();
  getEbusController().fetchDeviceInfo(
      [&writer](const ebus::DeviceInfo& device) { device.toJson(writer); });
  writer.endArray();
  writer.flush();
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleDevicesScan(httpd_req_t* req) {
  getEbusController().scanObservedDevices();
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  httpd_resp_sendstr_chunk(req, "{\"id\":\"scan\",\"status\":\"initiated\"}");
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleDevicesScanFull(httpd_req_t* req) {
  getEbusController().initFullScan(true);
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  httpd_resp_sendstr_chunk(req,
                           "{\"id\":\"scan_full\",\"status\":\"initiated\"}");
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleMetricsPage(httpd_req_t* req) {
  sendStatic(req, "text/html", metrics_html_start);
  return ESP_OK;
}

esp_err_t handleMetricsApi(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  getEbusController().fetchServiceStatus([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleMetricsReset(httpd_req_t* req) {
  getEbusController().resetMetrics();
  HttpUtils::sendResponse(req, "200 OK", "application/json;charset=utf-8",
                          "{\"id\":\"reset\",\"status\":\"successful\"}");
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
    std::vector<char> query(queryLen + 1, '\0');
    if (httpd_req_get_url_query_str(req, query.data(), query.size()) ==
        ESP_OK) {
      char sinceBuffer[32] = {0};
      if (httpd_query_key_value(query.data(), "since", sinceBuffer,
                                sizeof(sinceBuffer)) == ESP_OK) {
        sinceMillis = std::strtoull(sinceBuffer, nullptr, 10);
      }
    }
  }
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  logger.fetchLogsJson(
      [req](std::string_view chunk) {
        httpd_resp_send_chunk(req, chunk.data(), chunk.size());
      },
      sinceMillis);
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t handleLogsTimeRelation(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  logger.fetchTimeRelationJson([req](std::string_view chunk) {
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
  // Memory optimization: 4KB stack is sufficient for this server
  config.stack_size = 4096;
  // Resilience: Enable LRU purge to reclaim sockets from stalled clients
  config.lru_purge_enable = true;
  // CRITICAL for C3: Reduce socket count to save heap
  config.max_open_sockets = 2;
  // Give the networking stack more time to recover from packet loss
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
  RegisterUri("/status", HTTP_GET, handleStatusPage);
  RegisterUri("/adc", HTTP_GET, handleAdcPage);
  RegisterUri("/api/v1/status", HTTP_GET, handleStatusApi);
  RegisterUri("/api/v1/adc/raw", HTTP_GET, handleAdcRaw);
  RegisterUri("/api/v1/adc/enable", HTTP_POST, handleAdcEnable);
  RegisterUri("/api/v1/adc/disable", HTTP_POST, handleAdcDisable);
  RegisterUri("/api/v1/adc/state", HTTP_GET, handleAdcState);
  RegisterUri("/api/v1/wifi/scan", HTTP_POST, handleWifiScan);
  RegisterUri("/upgrade", HTTP_GET, handleUpgradePage);

#if defined(EBUS_INTERNAL)
  RegisterUri("/commands", HTTP_GET, handleCommandsPage);
  RegisterUri("/api/v1/commands", HTTP_GET, handleCommands);
  RegisterUri("/api/v1/commands/evaluate", HTTP_POST, handleCommandsEvaluate);
  RegisterUri("/api/v1/commands/insert", HTTP_POST, handleCommandsInsert);
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
  RegisterUri("/api/v1/metrics", HTTP_GET, handleMetricsApi);
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
