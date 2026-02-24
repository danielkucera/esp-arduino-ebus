#include "Adc.hpp"

#include <cstring>

#include <Arduino.h>
#include <WebServer.h>
#include <driver/adc.h>
#include <esp_err.h>

#include "Logger.hpp"

Adc adc;

static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_DEFAULT = 30000;
static constexpr uint8_t ADC_PATTERN_COUNT = 2;  // GPIO0 + GPIO1
static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_MIN = 600;
static constexpr uint32_t ADC_SAMPLE_FREQ_HZ_MAX = 83333;
static constexpr uint32_t SAMPLE_COUNT_DEFAULT = 2400;

bool Adc::begin() {
  if (configured) return true;

  esp_err_t err = adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
  if (err != ESP_OK) {
    logError("adc1_config_channel_atten(GPIO0)", err);
    configured = false;
    return false;
  }

  err = adc1_config_channel_atten(ADC1_CHANNEL_1, ADC_ATTEN_DB_11);
  if (err != ESP_OK) {
    logError("adc1_config_channel_atten(GPIO1)", err);
    configured = false;
    return false;
  }

  adc_digi_init_config_t initConfig = {};
  initConfig.max_store_buf_size = DMA_STORE_BUFFER_BYTES;
  initConfig.conv_num_each_intr = 256;
  initConfig.adc1_chan_mask = (1U << ADC1_CHANNEL_0) | (1U << ADC1_CHANNEL_1);
  initConfig.adc2_chan_mask = 0;

  err = adc_digi_initialize(&initConfig);
  if (err != ESP_OK) {
    logError("adc_digi_initialize", err);
    configured = false;
    return false;
  }

  adc_digi_pattern_config_t adcPattern[ADC_PATTERN_COUNT] = {};
  adcPattern[0].atten = ADC_ATTEN_DB_11;
  adcPattern[0].channel = ADC_CHANNEL_0;  // GPIO0
  adcPattern[0].unit = ADC_UNIT_1 - 1;
  adcPattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

  adcPattern[1].atten = ADC_ATTEN_DB_11;
  adcPattern[1].channel = ADC_CHANNEL_1;  // GPIO1
  adcPattern[1].unit = ADC_UNIT_1 - 1;
  adcPattern[1].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

  if (!configureController(ADC_SAMPLE_FREQ_HZ_DEFAULT)) {
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

bool Adc::configureController(uint32_t sampleRate) const {
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;

  adc_digi_pattern_config_t adcPattern[ADC_PATTERN_COUNT] = {};
  adcPattern[0].atten = ADC_ATTEN_DB_11;
  adcPattern[0].channel = ADC_CHANNEL_0;  // GPIO0
  adcPattern[0].unit = ADC_UNIT_1 - 1;
  adcPattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

  adcPattern[1].atten = ADC_ATTEN_DB_11;
  adcPattern[1].channel = ADC_CHANNEL_1;  // GPIO1
  adcPattern[1].unit = ADC_UNIT_1 - 1;
  adcPattern[1].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

  adc_digi_configuration_t config = {};
  config.conv_limit_en = false;
  config.conv_limit_num = 0;
  config.pattern_num = ADC_PATTERN_COUNT;
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

bool Adc::collectSamples(std::vector<uint16_t>& gpio1,
                         std::vector<uint16_t>& gpio0, uint32_t sampleRate,
                         uint32_t sampleCount) const {
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;
  if (sampleCount == 0) sampleCount = SAMPLE_COUNT_DEFAULT;

  if (!configureController(sampleRate)) return false;
  if (!startCapture()) return false;

  gpio1.clear();
  gpio0.clear();

  uint8_t dmaChunk[256];
  size_t frameCount = 0;

  while (frameCount < sampleCount) {
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

      if (sample.type2.channel == ADC_CHANNEL_1)
        gpio1.push_back(sample.type2.data);
      else if (sample.type2.channel == ADC_CHANNEL_0)
        gpio0.push_back(sample.type2.data);

      ++frameCount;
      if (frameCount >= sampleCount) break;
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

const std::string Adc::getJson(uint32_t sampleRate, uint32_t sampleCount) const {
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;
  if (sampleCount == 0) sampleCount = SAMPLE_COUNT_DEFAULT;

  std::vector<uint16_t> samples1;
  std::vector<uint16_t> samples0;

  if (!collectSamples(samples1, samples0, sampleRate, sampleCount))
    return "{\"error\":\"capture failed\"}";

  std::string payload = "{\"gpio\":1,\"buffer_bytes\":";
  payload += std::to_string(SAMPLE_BUFFER_BYTES);
  payload += ",\"sample_rate\":";
  payload += std::to_string(sampleRate);
  payload += ",\"sample_count\":";
  payload += std::to_string(sampleCount);
  payload += ",\"samples\":[";

  for (size_t i = 0; i < samples1.size(); ++i) {
    if (i > 0) payload += ",";
    payload += std::to_string(samples1[i]);
  }

  payload += "],\"samples_gpio0\":[";

  for (size_t i = 0; i < samples0.size(); ++i) {
    if (i > 0) payload += ",";
    payload += std::to_string(samples0[i]);
  }

  payload += "]}";
  return payload;
}

void Adc::streamJson(WebServer& server, uint32_t sampleRate,
                     uint32_t sampleCount) const {
  if (sampleRate < ADC_SAMPLE_FREQ_HZ_MIN) sampleRate = ADC_SAMPLE_FREQ_HZ_MIN;
  if (sampleRate > ADC_SAMPLE_FREQ_HZ_MAX) sampleRate = ADC_SAMPLE_FREQ_HZ_MAX;
  if (sampleCount == 0) sampleCount = SAMPLE_COUNT_DEFAULT;

  std::vector<uint16_t> samples1;
  std::vector<uint16_t> samples0;
  if (!collectSamples(samples1, samples0, sampleRate, sampleCount)) {
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
  append(",\"sample_count\":");
  append(String(sampleCount));
  append(",\"samples\":[");

  for (size_t i = 0; i < samples1.size(); ++i) {
    if (i > 0) append(",");
    append(String(samples1[i]));
  }

  append("],\"samples_gpio0\":[");

  for (size_t i = 0; i < samples0.size(); ++i) {
    if (i > 0) append(",");
    append(String(samples0[i]));
  }

  append("]}");
  flushChunk();
}
