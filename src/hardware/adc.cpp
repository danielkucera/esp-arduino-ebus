#include "hardware/adc.hpp"

#include <esp_adc/adc_continuous.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <soc/soc_caps.h>

#include <cstring>

#include "system/logger.hpp"

Adc adc;

namespace {
static constexpr uint32_t adc_sample_freq_hz_default = 30000;
#if defined(SOC_ADC_SAMPLE_FREQ_THRES_LOW)
static constexpr uint32_t adc_sample_freq_hz_min =
    SOC_ADC_SAMPLE_FREQ_THRES_LOW;
#else
static constexpr uint32_t adc_sample_freq_hz_min = 600;
#endif

#if defined(SOC_ADC_SAMPLE_FREQ_THRES_HIGH)
static constexpr uint32_t adc_sample_freq_hz_max =
    SOC_ADC_SAMPLE_FREQ_THRES_HIGH;
#else
static constexpr uint32_t adc_sample_freq_hz_max = 200000;
#endif
static constexpr uint32_t adc_channel_mask_all = 0x1F;      // GPIO0..4
static constexpr uint32_t adc_channel_mask_default = 0x03;  // GPIO0,1

constexpr uint32_t adc_samples_per_channel_fallback = 2400;

constexpr uint32_t adc_no_progress_multiplier = 4;
constexpr uint32_t adc_no_progress_base = 1000;
constexpr uint32_t adc_no_progress_timeout_min_ms = 3000;
constexpr uint32_t adc_no_progress_timeout_max_ms = 20000;

constexpr uint32_t adc_hard_timeout_multiplier = 20;
constexpr uint32_t adc_hard_timeout_base = 3000;
constexpr uint32_t adc_hard_timeout_min_ms = 8000;
constexpr uint32_t adc_hard_timeout_max_ms = 60000;
}  // namespace

bool Adc::begin() {
  if (configured_) return true;

  adc_continuous_handle_cfg_t handle_config = {};
  handle_config.max_store_buf_size = dma_store_buffer_bytes_;
  handle_config.conv_frame_size = raw_frame_bytes_;
  handle_config.flags.flush_pool = 1;

  esp_err_t err = adc_continuous_new_handle(&handle_config, &adc_handle_);
  if (err != ESP_OK) {
    logError("adc_continuous_new_handle", err);
    configured_ = false;
    return false;
  }

  if (!configureController(adc_sample_freq_hz_default,
                           adc_channel_mask_default)) {
    adc_continuous_deinit(adc_handle_);
    adc_handle_ = nullptr;
    configured_ = false;
    return false;
  }

  configured_ = true;
  capturing_ = false;
  return true;
}

void Adc::stop() {
  if (!configured_) return;
  if (capturing_) stopCapture();
  if (adc_handle_ != nullptr) {
    const esp_err_t err = adc_continuous_deinit(adc_handle_);
    if (err != ESP_OK) logError("adc_continuous_deinit", err);
    adc_handle_ = nullptr;
  }
  configured_ = false;
}

bool Adc::startCapture() const {
  if (!configured_ || adc_handle_ == nullptr) return false;
  if (capturing_) return true;

  const esp_err_t err = adc_continuous_start(adc_handle_);
  if (err != ESP_OK) {
    logError("adc_continuous_start", err);
    capturing_ = false;
    return false;
  }
  capturing_ = true;
  return true;
}

void Adc::stopCapture() const {
  if (!capturing_) return;
  capturing_ = false;  // mark before call so re-entrant calls are safe
  if (adc_handle_ != nullptr) {
    const esp_err_t err = adc_continuous_stop(adc_handle_);
    if (err != ESP_OK) logError("adc_continuous_stop", err);
  }
}

bool Adc::configureController(uint32_t sample_rate,
                              uint32_t channel_mask) const {
  if (adc_handle_ == nullptr) return false;
  if (sample_rate < adc_sample_freq_hz_min)
    sample_rate = adc_sample_freq_hz_min;
  if (sample_rate > adc_sample_freq_hz_max)
    sample_rate = adc_sample_freq_hz_max;
  channel_mask &= adc_channel_mask_all;
  if (channel_mask == 0) channel_mask = adc_channel_mask_default;

  std::memset(adc_pattern_, 0, sizeof(adc_pattern_));
  uint8_t pattern_count = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channel_mask & (1U << ch)) == 0) continue;
    adc_pattern_[pattern_count].atten = ADC_ATTEN_DB_12;
    adc_pattern_[pattern_count].channel = ch;
    adc_pattern_[pattern_count].unit = ADC_UNIT_1;
    adc_pattern_[pattern_count].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    ++pattern_count;
  }
  if (pattern_count == 0) return false;

  adc_continuous_config_t config = {};
  config.pattern_num = pattern_count;
  config.adc_pattern = adc_pattern_;
  config.sample_freq_hz = sample_rate;
  config.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  config.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;

  const esp_err_t err = adc_continuous_config(adc_handle_, &config);
  if (err != ESP_OK) {
    logError("adc_continuous_config", err);
    return false;
  }
  return true;
}

void Adc::logError(const char* stage, int err) {
  char buf[128];
  snprintf(buf, sizeof(buf), "ADC: %s: %s", stage, esp_err_to_name(err));
  logger.error(buf);
}

bool Adc::isRunning() const { return configured_; }

uint32_t Adc::effectivePerChannelSampleRate(uint32_t sample_rate,
                                            uint32_t channel_mask) {
  if (sample_rate < adc_sample_freq_hz_min)
    sample_rate = adc_sample_freq_hz_min;
  if (sample_rate > adc_sample_freq_hz_max)
    sample_rate = adc_sample_freq_hz_max;
  channel_mask &= adc_channel_mask_all;
  if (channel_mask == 0) channel_mask = adc_channel_mask_default;

  uint32_t num_active_channels = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channel_mask & (1U << ch)) != 0) ++num_active_channels;
  }
  if (num_active_channels == 0) num_active_channels = 1;

  uint64_t controller_sample_rate =
      static_cast<uint64_t>(sample_rate) * num_active_channels;
  if (controller_sample_rate < adc_sample_freq_hz_min)
    controller_sample_rate = adc_sample_freq_hz_min;
  if (controller_sample_rate > adc_sample_freq_hz_max)
    controller_sample_rate = adc_sample_freq_hz_max;

  uint32_t effective_per_channel_rate =
      static_cast<uint32_t>(controller_sample_rate / num_active_channels);
  return effective_per_channel_rate == 0 ? 1 : effective_per_channel_rate;
}

bool Adc::streamRaw(const ebus::JsonChunkVisitor& visitor, uint32_t sample_rate,
                    uint32_t samples_per_channel, uint32_t channel_mask) const {
  if (sample_rate < adc_sample_freq_hz_min)
    sample_rate = adc_sample_freq_hz_min;
  if (sample_rate > adc_sample_freq_hz_max)
    sample_rate = adc_sample_freq_hz_max;
  if (samples_per_channel == 0)
    samples_per_channel = adc_samples_per_channel_fallback;
  channel_mask &= adc_channel_mask_all;
  if (channel_mask == 0) channel_mask = adc_channel_mask_default;

  // Count active channels first so requested sampleRate can be interpreted
  // as per-channel rate even in multi-channel scans.
  uint32_t num_active_channels = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channel_mask & (1U << ch)) != 0) ++num_active_channels;
  }
  if (num_active_channels == 0) num_active_channels = 1;

  uint64_t controller_sample_rate =
      static_cast<uint64_t>(
          effectivePerChannelSampleRate(sample_rate, channel_mask)) *
      num_active_channels;
  if (controller_sample_rate < adc_sample_freq_hz_min)
    controller_sample_rate = adc_sample_freq_hz_min;
  if (controller_sample_rate > adc_sample_freq_hz_max)
    controller_sample_rate = adc_sample_freq_hz_max;

  // Reconfigure safely in INIT state.
  stopCapture();
  if (!configureController(static_cast<uint32_t>(controller_sample_rate),
                           channel_mask))
    return false;
  if (!startCapture()) return false;

  // samplesPerChannel is per-channel; total bytes accounts for all active
  // channels.
  const uint64_t total_samples =
      static_cast<uint64_t>(samples_per_channel) * num_active_channels;
  const uint64_t target_bytes = total_samples * result_bytes;
  uint64_t sent_bytes = 0;

  const uint32_t effective_per_channel_rate =
      effectivePerChannelSampleRate(sample_rate, channel_mask);

  const uint32_t expected_duration_ms = static_cast<uint32_t>(
      (static_cast<uint64_t>(samples_per_channel) * 1000ULL) /
      effective_per_channel_rate);
  uint32_t no_progress_timeout_ms =
      expected_duration_ms * adc_no_progress_multiplier + adc_no_progress_base;
  if (no_progress_timeout_ms < adc_no_progress_timeout_min_ms)
    no_progress_timeout_ms = adc_no_progress_timeout_min_ms;
  if (no_progress_timeout_ms > adc_no_progress_timeout_max_ms)
    no_progress_timeout_ms = adc_no_progress_timeout_max_ms;

  uint32_t hard_timeout_ms =
      expected_duration_ms * adc_hard_timeout_multiplier +
      adc_hard_timeout_base;
  if (hard_timeout_ms < adc_hard_timeout_min_ms)
    hard_timeout_ms = adc_hard_timeout_min_ms;
  if (hard_timeout_ms > adc_hard_timeout_max_ms)
    hard_timeout_ms = adc_hard_timeout_max_ms;

  const uint64_t start_us = esp_timer_get_time();
  uint64_t last_progress_us = start_us;

  uint32_t tx_fill = 0;
  while (sent_bytes < target_bytes) {
    const uint64_t elapsed_ms =
        static_cast<uint64_t>((esp_timer_get_time() - start_us) / 1000ULL);
    const uint64_t no_progress_ms = static_cast<uint64_t>(
        (esp_timer_get_time() - last_progress_us) / 1000ULL);
    if (no_progress_ms > no_progress_timeout_ms || elapsed_ms > hard_timeout_ms)
      break;

    uint32_t bytes_read = 0;
    esp_err_t err = adc_continuous_read(adc_handle_, dma_buffer_,
                                        raw_frame_bytes_, &bytes_read, 10);

    if (err == ESP_ERR_TIMEOUT || bytes_read == 0) {
      continue;
    }

    if (err == ESP_ERR_INVALID_STATE) {
      // Ringbuffer full: drain one frame and retry.
      uint32_t drained = 0;
      adc_continuous_read(adc_handle_, dma_buffer_, raw_frame_bytes_, &drained,
                          0);
      continue;
    }

    if (err != ESP_OK) break;

    uint32_t parsed_samples = 0;
    err = adc_continuous_parse_data(adc_handle_, dma_buffer_, bytes_read,
                                    parsed_buffer_, &parsed_samples);
    if (err != ESP_OK) {
      logError("adc_continuous_parse_data", err);
      break;
    }

    for (uint32_t i = 0; i < parsed_samples && sent_bytes < target_bytes; ++i) {
      if (!parsed_buffer_[i].valid || parsed_buffer_[i].unit != ADC_UNIT_1)
        continue;

      const uint8_t channel = static_cast<uint8_t>(parsed_buffer_[i].channel);
      if (channel > 4) continue;
      // Only send channels that were requested in the mask.
      if ((channel_mask & (1U << channel)) == 0) continue;

      const uint16_t packed = static_cast<uint16_t>(
          (static_cast<uint16_t>(parsed_buffer_[i].raw_data) & 0x0FFFU) |
          (static_cast<uint16_t>(channel) << 13));

      if (tx_fill + result_bytes > raw_http_chunk_bytes_) {
        visitor(std::string_view(reinterpret_cast<const char*>(tx_buffer_),
                                 tx_fill));
        tx_fill = 0;
      }
      std::memcpy(tx_buffer_ + tx_fill, &packed, result_bytes);
      tx_fill += result_bytes;
      sent_bytes += result_bytes;
    }
    last_progress_us = esp_timer_get_time();
  }

  if (tx_fill > 0) {
    visitor(
        std::string_view(reinterpret_cast<const char*>(tx_buffer_), tx_fill));
  }

  stopCapture();
  return sent_bytes >= target_bytes;
}
