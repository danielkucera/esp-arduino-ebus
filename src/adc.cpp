#include "adc.hpp"

#include <esp_adc/adc_continuous.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <soc/soc_caps.h>

#include <cstring>

#include "logger.hpp"

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

  adc_continuous_handle_cfg_t handleConfig = {};
  handleConfig.max_store_buf_size = dma_store_buffer_bytes;
  handleConfig.conv_frame_size = adc_raw_frame_bytes;
  handleConfig.flags.flush_pool = 1;

  esp_err_t err = adc_continuous_new_handle(&handleConfig, &adc_handle_);
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

bool Adc::configureController(uint32_t sampleRate, uint32_t channelMask) const {
  if (adc_handle_ == nullptr) return false;
  if (sampleRate < adc_sample_freq_hz_min) sampleRate = adc_sample_freq_hz_min;
  if (sampleRate > adc_sample_freq_hz_max) sampleRate = adc_sample_freq_hz_max;
  channelMask &= adc_channel_mask_all;
  if (channelMask == 0) channelMask = adc_channel_mask_default;

  std::memset(adc_pattern_, 0, sizeof(adc_pattern_));
  uint8_t patternCount = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channelMask & (1U << ch)) == 0) continue;
    adc_pattern_[patternCount].atten = ADC_ATTEN_DB_12;
    adc_pattern_[patternCount].channel = ch;
    adc_pattern_[patternCount].unit = ADC_UNIT_1;
    adc_pattern_[patternCount].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    ++patternCount;
  }
  if (patternCount == 0) return false;

  adc_continuous_config_t config = {};
  config.pattern_num = patternCount;
  config.adc_pattern = adc_pattern_;
  config.sample_freq_hz = sampleRate;
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

uint32_t Adc::effectivePerChannelSampleRate(uint32_t sampleRate,
                                            uint32_t channelMask) {
  if (sampleRate < adc_sample_freq_hz_min) sampleRate = adc_sample_freq_hz_min;
  if (sampleRate > adc_sample_freq_hz_max) sampleRate = adc_sample_freq_hz_max;
  channelMask &= adc_channel_mask_all;
  if (channelMask == 0) channelMask = adc_channel_mask_default;

  uint32_t numActiveChannels = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channelMask & (1U << ch)) != 0) ++numActiveChannels;
  }
  if (numActiveChannels == 0) numActiveChannels = 1;

  uint64_t controllerSampleRate =
      static_cast<uint64_t>(sampleRate) * numActiveChannels;
  if (controllerSampleRate < adc_sample_freq_hz_min)
    controllerSampleRate = adc_sample_freq_hz_min;
  if (controllerSampleRate > adc_sample_freq_hz_max)
    controllerSampleRate = adc_sample_freq_hz_max;

  uint32_t effectivePerChannelRate =
      static_cast<uint32_t>(controllerSampleRate / numActiveChannels);
  return effectivePerChannelRate == 0 ? 1 : effectivePerChannelRate;
}

bool Adc::streamRaw(const ebus::JsonChunkVisitor& visitor, uint32_t sampleRate,
                    uint32_t samplesPerChannel, uint32_t channelMask) const {
  if (sampleRate < adc_sample_freq_hz_min) sampleRate = adc_sample_freq_hz_min;
  if (sampleRate > adc_sample_freq_hz_max) sampleRate = adc_sample_freq_hz_max;
  if (samplesPerChannel == 0)
    samplesPerChannel = adc_samples_per_channel_fallback;
  channelMask &= adc_channel_mask_all;
  if (channelMask == 0) channelMask = adc_channel_mask_default;

  // Count active channels first so requested sampleRate can be interpreted
  // as per-channel rate even in multi-channel scans.
  uint32_t numActiveChannels = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channelMask & (1U << ch)) != 0) ++numActiveChannels;
  }
  if (numActiveChannels == 0) numActiveChannels = 1;

  uint64_t controllerSampleRate =
      static_cast<uint64_t>(
          effectivePerChannelSampleRate(sampleRate, channelMask)) *
      numActiveChannels;
  if (controllerSampleRate < adc_sample_freq_hz_min)
    controllerSampleRate = adc_sample_freq_hz_min;
  if (controllerSampleRate > adc_sample_freq_hz_max)
    controllerSampleRate = adc_sample_freq_hz_max;

  // Reconfigure safely in INIT state.
  stopCapture();
  if (!configureController(static_cast<uint32_t>(controllerSampleRate),
                           channelMask))
    return false;
  if (!startCapture()) return false;

  // samplesPerChannel is per-channel; total bytes accounts for all active
  // channels.
  const uint64_t totalSamples =
      static_cast<uint64_t>(samplesPerChannel) * numActiveChannels;
  const uint64_t targetBytes = totalSamples * result_bytes;
  uint64_t sentBytes = 0;

  const uint32_t effectivePerChannelRate =
      effectivePerChannelSampleRate(sampleRate, channelMask);

  const uint32_t expectedDurationMs = static_cast<uint32_t>(
      (static_cast<uint64_t>(samplesPerChannel) * 1000ULL) /
      effectivePerChannelRate);
  uint32_t noProgressTimeoutMs =
      expectedDurationMs * adc_no_progress_multiplier + adc_no_progress_base;
  if (noProgressTimeoutMs < adc_no_progress_timeout_min_ms)
    noProgressTimeoutMs = adc_no_progress_timeout_min_ms;
  if (noProgressTimeoutMs > adc_no_progress_timeout_max_ms)
    noProgressTimeoutMs = adc_no_progress_timeout_max_ms;

  uint32_t hardTimeoutMs =
      expectedDurationMs * adc_hard_timeout_multiplier + adc_hard_timeout_base;
  if (hardTimeoutMs < adc_hard_timeout_min_ms)
    hardTimeoutMs = adc_hard_timeout_min_ms;
  if (hardTimeoutMs > adc_hard_timeout_max_ms)
    hardTimeoutMs = adc_hard_timeout_max_ms;

  const uint64_t startUs = esp_timer_get_time();
  uint64_t lastProgressUs = startUs;

  uint32_t txFill = 0;
  while (sentBytes < targetBytes) {
    const uint64_t elapsedMs =
        static_cast<uint64_t>((esp_timer_get_time() - startUs) / 1000ULL);
    const uint64_t noProgressMs = static_cast<uint64_t>(
        (esp_timer_get_time() - lastProgressUs) / 1000ULL);
    if (noProgressMs > noProgressTimeoutMs || elapsedMs > hardTimeoutMs) break;

    uint32_t bytesRead = 0;
    esp_err_t err = adc_continuous_read(adc_handle_, dma_buffer_,
                                        adc_raw_frame_bytes, &bytesRead, 10);

    if (err == ESP_ERR_TIMEOUT || bytesRead == 0) {
      continue;
    }

    if (err == ESP_ERR_INVALID_STATE) {
      // Ringbuffer full: drain one frame and retry.
      uint32_t drained = 0;
      adc_continuous_read(adc_handle_, dma_buffer_, adc_raw_frame_bytes,
                          &drained, 0);
      continue;
    }

    if (err != ESP_OK) break;

    uint32_t parsedSamples = 0;
    err = adc_continuous_parse_data(adc_handle_, dma_buffer_, bytesRead,
                                    parsed_buffer_, &parsedSamples);
    if (err != ESP_OK) {
      logError("adc_continuous_parse_data", err);
      break;
    }

    for (uint32_t i = 0; i < parsedSamples && sentBytes < targetBytes; ++i) {
      if (!parsed_buffer_[i].valid || parsed_buffer_[i].unit != ADC_UNIT_1)
        continue;

      const uint8_t channel = static_cast<uint8_t>(parsed_buffer_[i].channel);
      if (channel > 4) continue;
      // Only send channels that were requested in the mask.
      if ((channelMask & (1U << channel)) == 0) continue;

      const uint16_t packed = static_cast<uint16_t>(
          (static_cast<uint16_t>(parsed_buffer_[i].raw_data) & 0x0FFFU) |
          (static_cast<uint16_t>(channel) << 13));

      if (txFill + result_bytes > adc_raw_http_chunk_bytes) {
        visitor(std::string_view(reinterpret_cast<const char*>(tx_buffer_),
                                 txFill));
        txFill = 0;
      }
      std::memcpy(tx_buffer_ + txFill, &packed, result_bytes);
      txFill += result_bytes;
      sentBytes += result_bytes;
    }
    lastProgressUs = esp_timer_get_time();
  }

  if (txFill > 0) {
    visitor(
        std::string_view(reinterpret_cast<const char*>(tx_buffer_), txFill));
  }

  stopCapture();
  return sentBytes >= targetBytes;
}
