#include "api/adc_api.hpp"

#include <esp_timer.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "hardware/adc.hpp"
#include "network/http.hpp"
#include "network/http_utils.hpp"

namespace {

// cppcheck-suppress syntaxError
extern const char adc_html_start[] asm("_binary_adc_html_start");

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
  if (req == nullptr) return 0x03;

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

}  // namespace

AdcApi* AdcApi::instance_ = nullptr;

AdcApi::AdcApi(Adc& adc) : adc_(adc) { instance_ = this; }

bool AdcApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  registerUri("/adc", HTTP_GET, handleAdcPage);
  registerUri("/api/v1/adc/raw", HTTP_GET, handleAdcRaw);
  registerUri("/api/v1/adc/enable", HTTP_POST, handleAdcEnable);
  registerUri("/api/v1/adc/disable", HTTP_POST, handleAdcDisable);
  registerUri("/api/v1/adc/state", HTTP_GET, handleAdcState);

  return true;
}

esp_err_t AdcApi::handleAdcPage(httpd_req_t* req) {
  HttpUtils::sendResponse(req, "200 OK", "text/html", adc_html_start);
  return ESP_OK;
}

esp_err_t AdcApi::handleAdcRaw(httpd_req_t* req) {
  if (!instance_->adc_.isRunning() && !instance_->adc_.begin()) {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "adc_raw",
                                 "ADC not running or failed to start");
    return ESP_OK;
  }

  const uint32_t sample_rate = parseAdcArg(req, "sample_rate", 30000);
  const uint32_t samples_per_channel = parseAdcArg(
      req, "samples_per_channel", parseAdcArg(req, "sample_count", 2400));
  const uint32_t channel_mask = parseAdcChannelMask(req);
  const uint32_t effective_per_channel_rate =
      instance_->adc_.effectivePerChannelSampleRate(sample_rate, channel_mask);
  const uint32_t active_channel_count =
      static_cast<uint32_t>(__builtin_popcount(channel_mask & 0x1F));
  const uint32_t controller_rate =
      effective_per_channel_rate *
      (active_channel_count == 0 ? 1U : active_channel_count);

  const uint64_t capture_start_millis =
      static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);

  char tmp1[32], tmp2[32], tmp3[32], tmp4[32], tmp5[32], tmp6[32];
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "X-ADC-Format", "esp32c3-ch12c3-le16");
  HttpUtils::applyCustomHeaders(req);

  std::snprintf(tmp1, sizeof(tmp1), "%u",
                static_cast<unsigned>(effective_per_channel_rate));
  httpd_resp_set_hdr(req, "X-ADC-Sample-Rate", tmp1);

  std::snprintf(tmp2, sizeof(tmp2), "%u",
                static_cast<unsigned>(samples_per_channel));
  httpd_resp_set_hdr(req, "X-ADC-Samples", tmp2);

  std::snprintf(tmp3, sizeof(tmp3), "%u", static_cast<unsigned>(channel_mask));
  httpd_resp_set_hdr(req, "X-ADC-Channel-Mask", tmp3);

  std::snprintf(tmp4, sizeof(tmp4), "%u",
                static_cast<unsigned>(Adc::result_bytes));
  httpd_resp_set_hdr(req, "X-ADC-Result-Bytes", tmp4);
  std::snprintf(tmp5, sizeof(tmp5), "%llu",
                static_cast<unsigned long long>(capture_start_millis));
  httpd_resp_set_hdr(req, "X-ADC-Capture-Start-Millis", tmp5);
  std::snprintf(tmp6, sizeof(tmp6), "%u",
                static_cast<unsigned>(controller_rate));
  httpd_resp_set_hdr(req, "X-ADC-Controller-Sample-Rate", tmp6);

  if (!instance_->adc_.streamRaw(
          [req](std::string_view chunk) {
            httpd_resp_send_chunk(req, chunk.data(), chunk.size());
          },
          sample_rate, samples_per_channel, channel_mask))
    return ESP_FAIL;
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t AdcApi::handleAdcState(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  {
    ebus::detail::JsonWriter writer([req](std::string_view chunk) {
      httpd_resp_send_chunk(req, chunk.data(), chunk.size());
    });
    auto scope = writer.objectScope();
    writer.writeField("running", instance_->adc_.isRunning());
  }
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

esp_err_t AdcApi::handleAdcEnable(httpd_req_t* req) {
  if (instance_->adc_.begin()) {
    HttpUtils::sendSuccessResponse(req, "adc_enable");
  } else {
    HttpUtils::sendErrorResponse(req, "500 Internal Server Error", "adc_enable",
                                 "ADC enable failed");
  }
  return ESP_OK;
}

esp_err_t AdcApi::handleAdcDisable(httpd_req_t* req) {
  instance_->adc_.stop();
  HttpUtils::sendSuccessResponse(req, "adc_disable");
  return ESP_OK;
}
