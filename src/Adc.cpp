#include "Adc.hpp"

#include <cstring>

#include <Arduino.h>
#include <WebServer.h>
#include <driver/adc.h>
#include <esp_err.h>

#include "Logger.hpp"

Adc adc;

static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_DEFAULT = 30000;
static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_MIN = 600;
static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_MAX = 200000;
static constexpr uint32_t SAMPLES_PER_CHANNEL_DEFAULT = 2400;
static constexpr uint32_t ADC_CHANNEL_MASK_ALL = 0x1F;      // GPIO0..4
static constexpr uint32_t ADC_CHANNEL_MASK_DEFAULT = 0x03;  // GPIO0,1

bool Adc::begin() {
  if (configured) return true;

  for (uint8_t ch = 0; ch <= 4; ++ch) {
    esp_err_t err =
        adc1_config_channel_atten(static_cast<adc1_channel_t>(ch), ADC_ATTEN_DB_11);
    if (err != ESP_OK) {
      logError("adc1_config_channel_atten", err);
      configured = false;
      return false;
    }
  }

  adc_digi_init_config_t initConfig = {};
  initConfig.max_store_buf_size = DMA_STORE_BUFFER_BYTES;
  initConfig.conv_num_each_intr = 256;
  initConfig.adc1_chan_mask = ADC_CHANNEL_MASK_ALL;
  initConfig.adc2_chan_mask = 0;

  esp_err_t err = adc_digi_initialize(&initConfig);
  if (err != ESP_OK) {
    logError("adc_digi_initialize", err);
    configured = false;
    return false;
  }

  if (!configureController(ADC_SAMPLE_FREQ_HZ_DEFAULT, ADC_CHANNEL_MASK_DEFAULT)) {
    adc_digi_deinitialize();
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
  adc_digi_deinitialize();
  configured = false;
}

bool Adc::startCapture() const {
  if (!configured) return false;
  if (capturing) return true;

  const esp_err_t err = adc_digi_start();
  if (err != ESP_OK) {
    logError("adc_digi_start", err);
    capturing = false;
    return false;
  }
  capturing = true;
  return true;
}

void Adc::stopCapture() const {
  if (!capturing) return;
  adc_digi_stop();
  capturing = false;
}

bool Adc::configureController(uint32_t sampleRate, uint32_t channelMask) const {
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;
  channelMask &= ADC_CHANNEL_MASK_ALL;
  if (channelMask == 0) channelMask = ADC_CHANNEL_MASK_DEFAULT;

  adc_digi_pattern_config_t adcPattern[5] = {};
  uint8_t patternCount = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channelMask & (1U << ch)) == 0) continue;
    adcPattern[patternCount].atten = ADC_ATTEN_DB_11;
    adcPattern[patternCount].channel = ch;
    adcPattern[patternCount].unit = ADC_UNIT_1 - 1;
    adcPattern[patternCount].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    ++patternCount;
  }
  if (patternCount == 0) return false;

  adc_digi_configuration_t config = {};
  config.conv_limit_en = false;
  config.conv_limit_num = 0;
  config.pattern_num = patternCount;
  config.adc_pattern = adcPattern;
  config.sample_freq_hz = sampleRate;
  config.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  config.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;

  const esp_err_t err = adc_digi_controller_configure(&config);
  if (err != ESP_OK) {
    logError("adc_digi_controller_configure", err);
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

  if (!configureController(sampleRate, channelMask)) return false;
  if (!startCapture()) return false;

  for (uint8_t ch = 0; ch <= 4; ++ch) channels[ch].clear();

  uint8_t dmaChunk[256];
  uint32_t selectedChannels = 0;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channelMask & (1U << ch)) != 0) ++selectedChannels;
  }
  const uint64_t targetTotalSamples =
      static_cast<uint64_t>(samplesPerChannel) * selectedChannels;
  uint64_t collectedSamples = 0;

  while (collectedSamples < targetTotalSamples) {
    uint32_t bytesRead = 0;
    esp_err_t err =
        adc_digi_read_bytes(dmaChunk, sizeof(dmaChunk), &bytesRead, 20);

    if (err == ESP_ERR_TIMEOUT || bytesRead == 0) continue;

    if (err == ESP_ERR_INVALID_STATE) {
      if (shouldLogNow()) {
        logger.warn("ADC: overflow (ESP_ERR_INVALID_STATE), restarting ADC DMA");
      }
      stopCapture();
      if (!startCapture()) break;
      continue;
    }

    if (err != ESP_OK) {
      if (shouldLogNow()) logError("adc_digi_read_bytes", err);
      break;
    }

    for (uint32_t i = 0; i + RESULT_BYTES <= bytesRead; i += RESULT_BYTES) {
      adc_digi_output_data_t sample = {};
      std::memcpy(&sample, &dmaChunk[i], RESULT_BYTES);

      if (sample.type2.unit != 0) continue;

      const uint8_t channel = sample.type2.channel;
      if (channel <= 4 && (channelMask & (1U << channel))) {
        std::vector<uint16_t>& series = channels[channel];
        if (series.size() < samplesPerChannel) {
          series.push_back(sample.type2.data);
          ++collectedSamples;
        }
      }

      if (collectedSamples >= targetTotalSamples) break;
    }
  }

  stopCapture();
  return true;
}

void Adc::logError(const char* stage, int err) const {
  logger.error(String("ADC: ") + stage + ": " + esp_err_to_name(err));
}

bool Adc::shouldLogNow() const {
  const uint32_t now = millis();
  if (now - lastErrorLogMs >= 1000) {
    lastErrorLogMs = now;
    return true;
  }
  return false;
}

const bool Adc::isRunning() const { return configured; }

const std::string Adc::getJson(uint32_t sampleRate, uint32_t samplesPerChannel,
                               uint32_t channelMask) const {
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;
  if (samplesPerChannel == 0)
    samplesPerChannel = SAMPLES_PER_CHANNEL_DEFAULT;
  channelMask &= ADC_CHANNEL_MASK_ALL;
  if (channelMask == 0) channelMask = ADC_CHANNEL_MASK_DEFAULT;

  std::array<std::vector<uint16_t>, 5> channels;

  if (!collectSamples(channels, sampleRate, samplesPerChannel, channelMask))
    return "{\"error\":\"capture failed\"}";

  std::string payload = "{\"gpio\":1,\"buffer_bytes\":";
  payload += std::to_string(SAMPLE_BUFFER_BYTES);
  payload += ",\"sample_rate\":";
  payload += std::to_string(sampleRate);
  payload += ",\"samples_per_channel\":";
  payload += std::to_string(samplesPerChannel);
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

void Adc::streamJson(WebServer& server, uint32_t sampleRate,
                     uint32_t samplesPerChannel, uint32_t channelMask) const {
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;
  if (samplesPerChannel == 0)
    samplesPerChannel = SAMPLES_PER_CHANNEL_DEFAULT;
  channelMask &= ADC_CHANNEL_MASK_ALL;
  if (channelMask == 0) channelMask = ADC_CHANNEL_MASK_DEFAULT;

  std::array<std::vector<uint16_t>, 5> channels;
  if (!collectSamples(channels, sampleRate, samplesPerChannel, channelMask)) {
    server.sendContent("{\"error\":\"capture failed\"}");
    return;
  }

  String chunk;
  chunk.reserve(1024);

  auto flushChunk = [&]() {
    if (!chunk.isEmpty()) {
      server.sendContent(chunk);
      chunk = "";
    }
  };

  auto append = [&](const String& s) {
    chunk += s;
    if (chunk.length() > 900) flushChunk();
  };

  append("{\"gpio\":1,\"buffer_bytes\":");
  append(String(SAMPLE_BUFFER_BYTES));
  append(",\"sample_rate\":");
  append(String(sampleRate));
  append(",\"samples_per_channel\":");
  append(String(samplesPerChannel));
  append(",\"channels\":[");
  bool firstChannel = true;
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    if ((channelMask & (1U << ch)) == 0) continue;
    if (!firstChannel) append(",");
    append(String(ch));
    firstChannel = false;
  }
  append("],\"samples\":[");

  for (size_t i = 0; i < channels[1].size(); ++i) {
    if (i > 0) append(",");
    append(String(channels[1][i]));
  }
  for (uint8_t ch = 0; ch <= 4; ++ch) {
    append("],\"samples_gpio");
    append(String(ch));
    append("\":[");
    for (size_t i = 0; i < channels[ch].size(); ++i) {
      if (i > 0) append(",");
      append(String(channels[ch][i]));
    }
  }

  append("]}");
  flushChunk();
}
