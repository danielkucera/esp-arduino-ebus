#include "Adc.hpp"

#include <cstring>

#include <esp_adc/adc_continuous.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <soc/soc_caps.h>

#include "Logger.hpp"

Adc adc;

static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_DEFAULT = 30000;
#if defined(SOC_ADC_SAMPLE_FREQ_THRES_LOW)
static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_MIN = SOC_ADC_SAMPLE_FREQ_THRES_LOW;
#else
static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_MIN = 600;
#endif

#if defined(SOC_ADC_SAMPLE_FREQ_THRES_HIGH)
static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_MAX = SOC_ADC_SAMPLE_FREQ_THRES_HIGH;
#else
static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_MAX = 200000;
#endif
static constexpr uint32_t ADC_CHANNEL_MASK_ALL = 0x1F;      // GPIO0..4
static constexpr uint32_t ADC_CHANNEL_MASK_DEFAULT = 0x03;  // GPIO0,1
static constexpr uint32_t ADC_RAW_FRAME_BYTES = 1024;
static constexpr uint32_t ADC_RAW_HTTP_CHUNK_BYTES = 4096;
static constexpr uint32_t ADC_DMA_SAMPLE_BYTES = 4;

namespace {
adc_continuous_handle_t adcHandle = nullptr;
adc_digi_pattern_config_t adcPattern[5] = {};
}

bool Adc::begin() {
  if (configured) return true;

  adc_continuous_handle_cfg_t handleConfig = {};
  handleConfig.max_store_buf_size = DMA_STORE_BUFFER_BYTES;
  handleConfig.conv_frame_size = ADC_RAW_FRAME_BYTES;
  handleConfig.flags.flush_pool = 1;

  esp_err_t err = adc_continuous_new_handle(&handleConfig, &adcHandle);
  if (err != ESP_OK) {
    logError("adc_continuous_new_handle", err);
    configured = false;
    return false;
  }

  if (!configureController(ADC_SAMPLE_FREQ_HZ_DEFAULT, ADC_CHANNEL_MASK_DEFAULT)) {
    adc_continuous_deinit(adcHandle);
    adcHandle = nullptr;
    configured = false;
    return false;
  }

  configured = true;
  capturing = false;
  return true;
}

void Adc::stop() {
  if (!configured) return;
  if (capturing) stopCapture();
  if (adcHandle != nullptr) {
    const esp_err_t err = adc_continuous_deinit(adcHandle);
    if (err != ESP_OK) logError("adc_continuous_deinit", err);
    adcHandle = nullptr;
  }
  configured = false;
}

bool Adc::startCapture() const {
  if (!configured || adcHandle == nullptr) return false;
  if (capturing) return true;

  const esp_err_t err = adc_continuous_start(adcHandle);
  if (err != ESP_OK) {
    logError("adc_continuous_start", err);
    capturing = false;
    return false;
  }
  capturing = true;
  return true;
}

void Adc::stopCapture() const {
  if (!capturing) return;
  capturing = false;  // mark before call so re-entrant calls are safe
  if (adcHandle != nullptr) {
    const esp_err_t err = adc_continuous_stop(adcHandle);
    if (err != ESP_OK) logError("adc_continuous_stop", err);
  }
}

bool Adc::configureController(uint32_t sampleRate, uint32_t channelMask) const {
  if (adcHandle == nullptr) return false;
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;
  channelMask &= ADC_CHANNEL_MASK_ALL;
  if (channelMask == 0) channelMask = ADC_CHANNEL_MASK_DEFAULT;

  std::memset(adcPattern, 0, sizeof(adcPattern));
  uint8_t patternCount = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channelMask & (1U << ch)) == 0) continue;
    adcPattern[patternCount].atten = ADC_ATTEN_DB_12;
    adcPattern[patternCount].channel = ch;
    adcPattern[patternCount].unit = ADC_UNIT_1;
    adcPattern[patternCount].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    ++patternCount;
  }
  if (patternCount == 0) return false;

  adc_continuous_config_t config = {};
  config.pattern_num = patternCount;
  config.adc_pattern = adcPattern;
  config.sample_freq_hz = sampleRate;
  config.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  config.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;

  const esp_err_t err = adc_continuous_config(adcHandle, &config);
  if (err != ESP_OK) {
    logError("adc_continuous_config", err);
    return false;
  }
  return true;
}

void Adc::logError(const char* stage, int err) const {
  logger.error(std::string("ADC: ") + stage + ": " + esp_err_to_name(err));
}

bool Adc::isRunning() const { return configured; }

uint32_t Adc::effectivePerChannelSampleRate(uint32_t sampleRate,
                                            uint32_t channelMask) const {
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;
  channelMask &= ADC_CHANNEL_MASK_ALL;
  if (channelMask == 0) channelMask = ADC_CHANNEL_MASK_DEFAULT;

  uint32_t numActiveChannels = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channelMask & (1U << ch)) != 0) ++numActiveChannels;
  }
  if (numActiveChannels == 0) numActiveChannels = 1;

  uint64_t controllerSampleRate =
      static_cast<uint64_t>(sampleRate) * numActiveChannels;
  if (controllerSampleRate < ADC_SAMPLE_FREQ_HZ_MIN)
    controllerSampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (controllerSampleRate > ADC_SAMPLE_FREQ_HZ_MAX)
    controllerSampleRate = ADC_SAMPLE_FREQ_HZ_MAX;

  uint32_t effectivePerChannelRate =
      static_cast<uint32_t>(controllerSampleRate / numActiveChannels);
  return effectivePerChannelRate == 0 ? 1 : effectivePerChannelRate;
}

bool Adc::streamRaw(httpd_req_t* req, uint32_t sampleRate,
                    uint32_t samplesPerChannel, uint32_t channelMask) const {
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;
  if (samplesPerChannel == 0) samplesPerChannel = 2400;
  channelMask &= ADC_CHANNEL_MASK_ALL;
  if (channelMask == 0) channelMask = ADC_CHANNEL_MASK_DEFAULT;

  // Count active channels first so requested sampleRate can be interpreted
  // as per-channel rate even in multi-channel scans.
  uint32_t numActiveChannels = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channelMask & (1U << ch)) != 0) ++numActiveChannels;
  }
  if (numActiveChannels == 0) numActiveChannels = 1;

  uint64_t controllerSampleRate =
      static_cast<uint64_t>(effectivePerChannelSampleRate(sampleRate, channelMask)) *
      numActiveChannels;
  if (controllerSampleRate < ADC_SAMPLE_FREQ_HZ_MIN)
    controllerSampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (controllerSampleRate > ADC_SAMPLE_FREQ_HZ_MAX)
    controllerSampleRate = ADC_SAMPLE_FREQ_HZ_MAX;

  // Reconfigure safely in INIT state.
  stopCapture();
  if (!configureController(static_cast<uint32_t>(controllerSampleRate),
                           channelMask))
    return false;
  if (!startCapture()) return false;

  // samplesPerChannel is per-channel; total bytes accounts for all active channels.
  const uint64_t totalSamples = static_cast<uint64_t>(samplesPerChannel) * numActiveChannels;
  const uint64_t targetBytes = totalSamples * RESULT_BYTES;
  uint64_t sentBytes = 0;

    const uint32_t effectivePerChannelRate =
      effectivePerChannelSampleRate(sampleRate, channelMask);

  const uint32_t expectedDurationMs =
      static_cast<uint32_t>((static_cast<uint64_t>(samplesPerChannel) * 1000ULL) /
                            effectivePerChannelRate);
  uint32_t noProgressTimeoutMs = expectedDurationMs * 4U + 1000U;
  if (noProgressTimeoutMs < 3000U) noProgressTimeoutMs = 3000U;
  if (noProgressTimeoutMs > 20000U) noProgressTimeoutMs = 20000U;

  uint32_t hardTimeoutMs = expectedDurationMs * 20U + 3000U;
  if (hardTimeoutMs < 8000U) hardTimeoutMs = 8000U;
  if (hardTimeoutMs > 60000U) hardTimeoutMs = 60000U;

  const uint64_t startUs = esp_timer_get_time();
  uint64_t lastProgressUs = startUs;

  uint8_t dmaChunk[ADC_RAW_FRAME_BYTES];
  uint8_t txChunk[ADC_RAW_HTTP_CHUNK_BYTES];
  uint32_t txFill = 0;
  static adc_continuous_data_t parsed[ADC_RAW_FRAME_BYTES / ADC_DMA_SAMPLE_BYTES];
  while (sentBytes < targetBytes) {
    const uint64_t elapsedMs =
        static_cast<uint64_t>((esp_timer_get_time() - startUs) / 1000ULL);
    const uint64_t noProgressMs =
        static_cast<uint64_t>((esp_timer_get_time() - lastProgressUs) / 1000ULL);
    if (noProgressMs > noProgressTimeoutMs || elapsedMs > hardTimeoutMs) break;

    uint32_t bytesRead = 0;
    esp_err_t err = adc_continuous_read(adcHandle, dmaChunk, sizeof(dmaChunk),
                                        &bytesRead, 10);

    if (err == ESP_ERR_TIMEOUT || bytesRead == 0) {
      continue;
    }

    if (err == ESP_ERR_INVALID_STATE) {
      // Ringbuffer full: drain one frame and retry.
      uint32_t drained = 0;
      adc_continuous_read(adcHandle, dmaChunk, sizeof(dmaChunk), &drained, 0);
      continue;
    }

    if (err != ESP_OK) break;

    uint32_t parsedSamples = 0;
    err = adc_continuous_parse_data(adcHandle, dmaChunk, bytesRead, parsed,
                                    &parsedSamples);
    if (err != ESP_OK) {
      logError("adc_continuous_parse_data", err);
      break;
    }

    for (uint32_t i = 0; i < parsedSamples && sentBytes < targetBytes; ++i) {
      if (!parsed[i].valid || parsed[i].unit != ADC_UNIT_1) continue;

      const uint8_t channel = static_cast<uint8_t>(parsed[i].channel);
      if (channel > 4) continue;
      // Only send channels that were requested in the mask.
      if ((channelMask & (1U << channel)) == 0) continue;

        // Compact 16-bit word for UI decoder:
        // bits [11:0]=data, [12]=0, [15:13]=channel.
        const uint16_t packed =
          static_cast<uint16_t>((static_cast<uint16_t>(parsed[i].raw_data) & 0x0FFFU) |
                    (static_cast<uint16_t>(channel) << 13));

      if (txFill + RESULT_BYTES > sizeof(txChunk)) {
        if (httpd_resp_send_chunk(req, reinterpret_cast<const char*>(txChunk),
                                  txFill) != ESP_OK) {
          txFill = 0;
          goto raw_done;
        }
        txFill = 0;
      }

      std::memcpy(txChunk + txFill, &packed, RESULT_BYTES);
      txFill += RESULT_BYTES;
      sentBytes += RESULT_BYTES;
      lastProgressUs = esp_timer_get_time();
    }
  }

  if (txFill > 0) {
    if (httpd_resp_send_chunk(req, reinterpret_cast<const char*>(txChunk),
                              txFill) == ESP_OK) {
      txFill = 0;
    }
  }

raw_done:

  stopCapture();
  return sentBytes >= targetBytes;
}
