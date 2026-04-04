#include "Adc.hpp"

#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <sys/time.h>

#include <esp_adc/adc_continuous.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>

#include "Logger.hpp"

Adc adc;

static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_DEFAULT = 30000;
static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_MIN = 600;
static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_MAX = 200000;
static constexpr uint32_t SAMPLES_PER_CHANNEL_DEFAULT = 2400;
// Building bulk JSON in-memory is expensive on ESP32-C3. Keep this bounded.
static constexpr uint32_t SAMPLES_PER_CHANNEL_MAX_JSON = 800;
static constexpr uint32_t ADC_CHANNEL_MASK_ALL = 0x1F;      // GPIO0..4
static constexpr uint32_t ADC_CHANNEL_MASK_DEFAULT = 0x03;  // GPIO0,1
static constexpr time_t NTP_SYNCED_EPOCH_THRESHOLD = 1700000000;

namespace {
adc_continuous_handle_t adcHandle = nullptr;
adc_digi_pattern_config_t adcPattern[5] = {};
}

bool Adc::begin() {
  if (configured) return true;

  adc_continuous_handle_cfg_t handleConfig = {};
  handleConfig.max_store_buf_size = DMA_STORE_BUFFER_BYTES;
  handleConfig.conv_frame_size = 256;
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

bool Adc::collectSamples(std::array<std::vector<uint16_t>, 5>& channels,
                         uint32_t sampleRate, uint32_t samplesPerChannel,
                         uint32_t channelMask) const {
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;
  if (samplesPerChannel == 0) samplesPerChannel = SAMPLES_PER_CHANNEL_DEFAULT;
  channelMask &= ADC_CHANNEL_MASK_ALL;
  if (channelMask == 0) channelMask = ADC_CHANNEL_MASK_DEFAULT;

  // adc_continuous_config requires the driver to be in INIT (not started) state.
  stopCapture();
  if (!configureController(sampleRate, channelMask)) return false;
  if (!startCapture()) return false;

  for (uint8_t ch = 0; ch <= 4; ++ch) {
    channels[ch].clear();
    // Reserve upfront to avoid vector reallocation (and its peak double-alloc)
    // during capture, which would throw std::bad_alloc on heap exhaustion.
    if (channelMask & (1U << ch)) channels[ch].reserve(samplesPerChannel);
  }

  uint8_t dmaChunk[256];
  uint32_t selectedChannels = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channelMask & (1U << ch)) != 0) ++selectedChannels;
  }
  auto countBits = [](uint32_t mask) -> uint32_t {
    uint32_t count = 0;
    for (uint8_t b = 0; b < 5; ++b) {
      if ((mask & (1U << b)) != 0) ++count;
    }
    return count;
  };

  const uint64_t targetTotalSamples =
      static_cast<uint64_t>(samplesPerChannel) * selectedChannels;
  uint64_t collectedSamples = 0;
  uint32_t seenChannelMask = 0;
  uint64_t effectiveTargetSamples = targetTotalSamples;
  static constexpr uint32_t CHANNEL_DISCOVERY_WINDOW_MS = 300;

  // Guard against wedging the HTTP server task if ADC DMA stops producing
  // samples, while still allowing slow captures to complete.
  const uint32_t expectedDurationMs =
      static_cast<uint32_t>((targetTotalSamples * 1000ULL) / sampleRate);
  uint32_t noProgressTimeoutMs = expectedDurationMs * 4U + 1000U;
  if (noProgressTimeoutMs < 3000U) noProgressTimeoutMs = 3000U;
  if (noProgressTimeoutMs > 20000U) noProgressTimeoutMs = 20000U;

  uint32_t hardTimeoutMs = expectedDurationMs * 20U + 3000U;
  if (hardTimeoutMs < 8000U) hardTimeoutMs = 8000U;
  if (hardTimeoutMs > 60000U) hardTimeoutMs = 60000U;

  const uint64_t captureStartUs = esp_timer_get_time();
  uint64_t lastProgressUs = captureStartUs;
  uint32_t timeoutReads = 0;
  uint32_t invalidStateRestarts = 0;
  uint32_t okReads = 0;

  while (collectedSamples < targetTotalSamples) {
    const uint64_t elapsedMs =
        static_cast<uint64_t>((esp_timer_get_time() - captureStartUs) / 1000ULL);
    const uint64_t noProgressMs =
        static_cast<uint64_t>((esp_timer_get_time() - lastProgressUs) / 1000ULL);
    if (noProgressMs > noProgressTimeoutMs || elapsedMs > hardTimeoutMs) {
      if (shouldLogNow()) {
        char warnBuf[180];
        std::snprintf(warnBuf, sizeof(warnBuf),
                      "ADC: capture timeout got=%llu/%llu reads=%" PRIu32 " timeout=%" PRIu32 " restarts=%" PRIu32 " mask=%" PRIu32,
                      static_cast<unsigned long long>(collectedSamples),
                      static_cast<unsigned long long>(effectiveTargetSamples),
                      okReads, timeoutReads, invalidStateRestarts,
                      seenChannelMask);
        logger.warn(warnBuf);
      }
      break;
    }

    uint32_t bytesRead = 0;
    esp_err_t err =
      adc_continuous_read(adcHandle, dmaChunk, sizeof(dmaChunk), &bytesRead,
                20);

    if (err == ESP_ERR_TIMEOUT || bytesRead == 0) {
      ++timeoutReads;
      esp_rom_delay_us(1000);
      continue;
    }

    if (err == ESP_ERR_INVALID_STATE) {
      // Ringbuffer full: drain one frame and retry.
      if (shouldLogNow()) {
        logger.warn("ADC: pool overflow, draining");
      }
      ++invalidStateRestarts;
      uint32_t drained = 0;
      if (adcHandle != nullptr) {
        adc_continuous_read(adcHandle, dmaChunk, sizeof(dmaChunk), &drained, 0);
      }
      continue;
    }

    if (err != ESP_OK) {
      if (shouldLogNow()) logError("adc_continuous_read", err);
      break;
    }

    ++okReads;

    // Static so this 1 KB array lives in BSS rather than on the HTTP task stack.
    static adc_continuous_data_t parsed[64];
    uint32_t parsedSamples = 0;
    err = adc_continuous_parse_data(adcHandle, dmaChunk, bytesRead, parsed,
                                    &parsedSamples);
    if (err != ESP_OK) {
      if (shouldLogNow()) logError("adc_continuous_parse_data", err);
      continue;
    }

    for (uint32_t i = 0; i < parsedSamples; ++i) {
      if (!parsed[i].valid) continue;
      if (parsed[i].unit != ADC_UNIT_1) continue;

      const uint8_t channel = static_cast<uint8_t>(parsed[i].channel);
      if (channel <= 4 && (channelMask & (1U << channel))) {
        seenChannelMask |= (1U << channel);
        std::vector<uint16_t>& series = channels[channel];
        if (series.size() < samplesPerChannel) {
          series.push_back(static_cast<uint16_t>(parsed[i].raw_data));
          ++collectedSamples;
          lastProgressUs = esp_timer_get_time();
        }
      }

      if (collectedSamples >= effectiveTargetSamples) break;
    }

    if (elapsedMs >= CHANNEL_DISCOVERY_WINDOW_MS && seenChannelMask != 0) {
      const uint32_t seenChannels = countBits(seenChannelMask & channelMask);
      if (seenChannels > 0) {
        effectiveTargetSamples =
            static_cast<uint64_t>(samplesPerChannel) * seenChannels;
      }
    }

    if (collectedSamples >= effectiveTargetSamples) break;
  }

  stopCapture();
  return collectedSamples >= effectiveTargetSamples;
}

void Adc::logError(const char* stage, int err) const {
  logger.error(std::string("ADC: ") + stage + ": " + esp_err_to_name(err));
}

bool Adc::shouldLogNow() const {
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  if (now - lastErrorLogMs >= 1000) {
    lastErrorLogMs = now;
    return true;
  }
  return false;
}

bool Adc::isRunning() const { return configured; }

const std::string Adc::getJson(uint32_t sampleRate, uint32_t samplesPerChannel,
                               uint32_t channelMask) const {
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;
  if (samplesPerChannel == 0)
    samplesPerChannel = SAMPLES_PER_CHANNEL_DEFAULT;
  if (samplesPerChannel > SAMPLES_PER_CHANNEL_MAX_JSON)
    samplesPerChannel = SAMPLES_PER_CHANNEL_MAX_JSON;
  channelMask &= ADC_CHANNEL_MASK_ALL;
  if (channelMask == 0) channelMask = ADC_CHANNEL_MASK_DEFAULT;

  std::array<std::vector<uint16_t>, 5> channels;

  if (!collectSamples(channels, sampleRate, samplesPerChannel, channelMask))
    return "{\"error\":\"capture failed\"}";

  struct timeval now = {};
  gettimeofday(&now, nullptr);
  const bool ntpSynced = now.tv_sec >= NTP_SYNCED_EPOCH_THRESHOLD;
  const uint64_t captureEndEpochMs =
      ntpSynced ? (static_cast<uint64_t>(now.tv_sec) * 1000ULL) +
                      (static_cast<uint64_t>(now.tv_usec) / 1000ULL)
                : 0ULL;

  // Per-sample: up to 5 digits + comma = 6 chars. Legacy 'samples' field
  // duplicates channel-1, so account for (channelCount + 1) arrays total,
  // plus all five samples_gpioN fields (most empty). Add fixed overhead.
  uint8_t channelCount = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch)
    if (channelMask & (1U << ch)) ++channelCount;
  const size_t estimatedSize =
      512 + static_cast<size_t>(samplesPerChannel) * (channelCount + 6) * 6;

  // Avoid creating very large contiguous allocations in std::string.
  if (estimatedSize > 64U * 1024U) {
    return "{\"error\":\"adc payload too large; reduce samples\"}";
  }

  // Avoid abort-on-bad_alloc (exceptions are disabled in this firmware).
  const size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (largestBlock < estimatedSize + 8192 || freeHeap < estimatedSize + 16384) {
    return "{\"error\":\"insufficient heap for adc payload\"}";
  }

  std::string payload;
  payload.reserve(estimatedSize);
  payload = "{\"gpio\":1,\"buffer_bytes\":";
  payload += std::to_string(SAMPLE_BUFFER_BYTES);
  payload += ",\"sample_rate\":";
  payload += std::to_string(sampleRate);
  payload += ",\"samples_per_channel\":";
  payload += std::to_string(samplesPerChannel);
  payload += ",\"ntp_synced\":";
  payload += ntpSynced ? "true" : "false";
  payload += ",\"capture_end_epoch_ms\":";
  payload += std::to_string(captureEndEpochMs);
  payload += ",\"channels\":[";
  bool firstChannel = true;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channelMask & (1U << ch)) == 0) continue;
    if (!firstChannel) payload += ",";
    payload += std::to_string(ch);
    firstChannel = false;
  }
  payload += "],\"samples\":[";

  for (size_t i = 0; i < channels[1].size(); ++i) {
    if (i > 0) payload += ",";
    payload += std::to_string(channels[1][i]);
  }

  for (uint8_t ch = 0; ch <= 4; ++ch) {
    payload += "],\"samples_gpio";
    payload += std::to_string(ch);
    payload += "\":[";
    for (size_t i = 0; i < channels[ch].size(); ++i) {
      if (i > 0) payload += ",";
      payload += std::to_string(channels[ch][i]);
    }
  }

  payload += "]}";
  return payload;
}
