#include "Adc.hpp"

#include <cstring>
#include <vector>

#include <Arduino.h>
#include <driver/adc.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <WebServer.h>

#include "Logger.hpp"

Adc adc;

static constexpr uint32_t ADC_SAMPLE_FREQ_HZ = 30000;
static constexpr uint8_t ADC_PATTERN_COUNT = 2;  // GPIO0 + GPIO1

bool Adc::begin() {
  if (running) return true;

  esp_err_t err = adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
  if (err != ESP_OK) {
    logError("adc1_config_channel_atten(GPIO0)", err);
    running = false;
    return false;
  }

  err = adc1_config_channel_atten(ADC1_CHANNEL_1, ADC_ATTEN_DB_11);
  if (err != ESP_OK) {
    logError("adc1_config_channel_atten", err);
    running = false;
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
    running = false;
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

  adc_digi_configuration_t config = {};
  config.conv_limit_en = false;
  config.conv_limit_num = 0;
  config.pattern_num = ADC_PATTERN_COUNT;
  config.adc_pattern = adcPattern;
  config.sample_freq_hz = ADC_SAMPLE_FREQ_HZ;
  config.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  config.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;

  err = adc_digi_controller_configure(&config);
  if (err != ESP_OK) {
    logError("adc_digi_controller_configure", err);
    adc_digi_deinitialize();
    running = false;
    return false;
  }

  err = adc_digi_start();
  if (err != ESP_OK) {
    logError("adc_digi_start", err);
    adc_digi_deinitialize();
    running = false;
    return false;
  }

  if (bufferMutex == nullptr) {
    bufferMutex = xSemaphoreCreateMutex();
    if (bufferMutex == nullptr) {
      logger.error("ADC: xSemaphoreCreateMutex failed");
      adc_digi_stop();
      adc_digi_deinitialize();
      running = false;
      return false;
    }
  }

  if (captureTaskHandle == nullptr) {
    BaseType_t created = xTaskCreate(captureTask, "adcCapture", 4096, this, 1,
                                     reinterpret_cast<TaskHandle_t*>(&captureTaskHandle));
    if (created != pdPASS) {
      logger.error("ADC: xTaskCreate(adcCapture) failed");
      adc_digi_stop();
      adc_digi_deinitialize();
      running = false;
      return false;
    }
  }

  running = true;
  return true;
}

void Adc::stop() {
  if (!running) return;
  running = false;
  adc_digi_stop();
  adc_digi_deinitialize();
}

void Adc::captureTask(void* param) {
  Adc* self = static_cast<Adc*>(param);
  uint8_t dmaChunk[256];

  while (true) {
    if (!self->running) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    uint32_t bytesRead = 0;
    esp_err_t err =
        adc_digi_read_bytes(dmaChunk, sizeof(dmaChunk), &bytesRead, 1000);

    if (err == ESP_OK && bytesRead > 0) {
      self->pushRawBytes(dmaChunk, bytesRead);
      continue;
    }

    if (err == ESP_ERR_TIMEOUT) continue;

    if (err == ESP_ERR_INVALID_STATE) {
      self->recoverFromInvalidState();
      continue;
    }

    if (self->shouldLogNow()) self->logError("adc_digi_read_bytes", err);
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void Adc::pushRawBytes(const uint8_t* data, size_t len) {
  const size_t frameBytes = (len / RESULT_BYTES) * RESULT_BYTES;
  if (frameBytes == 0) return;

  xSemaphoreTake(static_cast<SemaphoreHandle_t>(bufferMutex), portMAX_DELAY);

  for (size_t i = 0; i < frameBytes; ++i) {
    rawBuffer[rawWriteIndex] = data[i];
    rawWriteIndex = (rawWriteIndex + 1) % SAMPLE_BUFFER_BYTES;
    if (rawBytesFilled < SAMPLE_BUFFER_BYTES) {
      ++rawBytesFilled;
    }
  }

  totalFrames += static_cast<uint32_t>(frameBytes / RESULT_BYTES);

  xSemaphoreGive(static_cast<SemaphoreHandle_t>(bufferMutex));
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

void Adc::recoverFromInvalidState() {
  if (!running) return;

  if (shouldLogNow()) {
    logger.warn("ADC: overflow (ESP_ERR_INVALID_STATE), restarting ADC DMA");
  }

  adc_digi_stop();
  esp_err_t err = adc_digi_start();
  if (err != ESP_OK && shouldLogNow()) {
    logError("adc_digi_start(recover)", err);
  }
}

const bool Adc::isRunning() const { return running; }

const std::string Adc::getJson() const {
  std::vector<uint8_t> snapshot(SAMPLE_BUFFER_BYTES);
  size_t snapshotBytes = 0;
  size_t snapshotWriteIndex = 0;
  uint32_t snapshotTotalFrames = 0;

  xSemaphoreTake(static_cast<SemaphoreHandle_t>(bufferMutex), portMAX_DELAY);
  std::memcpy(snapshot.data(), rawBuffer.data(), SAMPLE_BUFFER_BYTES);
  snapshotBytes = rawBytesFilled;
  snapshotWriteIndex = rawWriteIndex;
  snapshotTotalFrames = totalFrames;
  xSemaphoreGive(static_cast<SemaphoreHandle_t>(bufferMutex));

  const size_t validBytes = (snapshotBytes / RESULT_BYTES) * RESULT_BYTES;
  const size_t start =
      (snapshotWriteIndex + SAMPLE_BUFFER_BYTES - validBytes) %
      SAMPLE_BUFFER_BYTES;

  std::string payload = "{\"gpio\":1,\"buffer_bytes\":";
  payload += std::to_string(SAMPLE_BUFFER_BYTES);
  payload += ",\"raw_frame_count\":";
  payload += std::to_string(validBytes / RESULT_BYTES);
  payload += ",\"total_frames\":";
  payload += std::to_string(snapshotTotalFrames);
  payload += ",\"samples\":[";

  bool first = true;
  bool firstGpio0 = true;
  std::string gpio0Samples = "\"samples_gpio0\":[";

  for (size_t i = 0; i + RESULT_BYTES <= validBytes; i += RESULT_BYTES) {
    adc_digi_output_data_t sample = {};

    uint8_t frame[RESULT_BYTES];
    for (size_t b = 0; b < RESULT_BYTES; ++b) {
      frame[b] = snapshot[(start + i + b) % SAMPLE_BUFFER_BYTES];
    }
    std::memcpy(&sample, frame, RESULT_BYTES);

    if (sample.type2.unit == 0) {
      if (sample.type2.channel == ADC_CHANNEL_1) {
        if (!first) payload += ",";
        payload += std::to_string(sample.type2.data);
        first = false;
      } else if (sample.type2.channel == ADC_CHANNEL_0) {
        if (!firstGpio0) gpio0Samples += ",";
        gpio0Samples += std::to_string(sample.type2.data);
        firstGpio0 = false;
      }
    }
  }

  gpio0Samples += "]";
  payload += "],";
  payload += gpio0Samples;
  payload += "}";
  return payload;
}

void Adc::streamJson(WebServer& server) const {
  std::vector<uint8_t> snapshot(SAMPLE_BUFFER_BYTES);
  size_t snapshotBytes = 0;
  size_t snapshotWriteIndex = 0;
  uint32_t snapshotTotalFrames = 0;

  xSemaphoreTake(static_cast<SemaphoreHandle_t>(bufferMutex), portMAX_DELAY);
  std::memcpy(snapshot.data(), rawBuffer.data(), SAMPLE_BUFFER_BYTES);
  snapshotBytes = rawBytesFilled;
  snapshotWriteIndex = rawWriteIndex;
  snapshotTotalFrames = totalFrames;
  xSemaphoreGive(static_cast<SemaphoreHandle_t>(bufferMutex));

  const size_t validBytes = (snapshotBytes / RESULT_BYTES) * RESULT_BYTES;
  const size_t start =
      (snapshotWriteIndex + SAMPLE_BUFFER_BYTES - validBytes) %
      SAMPLE_BUFFER_BYTES;

  server.sendContent("{\"gpio\":1,\"buffer_bytes\":");
  server.sendContent(std::to_string(SAMPLE_BUFFER_BYTES).c_str());
  server.sendContent(",\"raw_frame_count\":");
  server.sendContent(std::to_string(validBytes / RESULT_BYTES).c_str());
  server.sendContent(",\"total_frames\":");
  server.sendContent(std::to_string(snapshotTotalFrames).c_str());
  server.sendContent(",\"samples\":[");

  bool first = true;
  bool firstGpio0 = true;
  for (size_t i = 0; i + RESULT_BYTES <= validBytes; i += RESULT_BYTES) {
    adc_digi_output_data_t sample = {};

    uint8_t frame[RESULT_BYTES];
    for (size_t b = 0; b < RESULT_BYTES; ++b) {
      frame[b] = snapshot[(start + i + b) % SAMPLE_BUFFER_BYTES];
    }
    std::memcpy(&sample, frame, RESULT_BYTES);

    if (sample.type2.unit == 0) {
      if (sample.type2.channel == ADC_CHANNEL_1) {
        if (!first) server.sendContent(",");
        first = false;
        server.sendContent(std::to_string(sample.type2.data).c_str());
      }
    }
  }

  server.sendContent("],\"samples_gpio0\":[");

  for (size_t i = 0; i + RESULT_BYTES <= validBytes; i += RESULT_BYTES) {
    adc_digi_output_data_t sample = {};

    uint8_t frame[RESULT_BYTES];
    for (size_t b = 0; b < RESULT_BYTES; ++b) {
      frame[b] = snapshot[(start + i + b) % SAMPLE_BUFFER_BYTES];
    }
    std::memcpy(&sample, frame, RESULT_BYTES);

    if (sample.type2.unit == 0 && sample.type2.channel == ADC_CHANNEL_0) {
      if (!firstGpio0) server.sendContent(",");
      firstGpio0 = false;
      server.sendContent(std::to_string(sample.type2.data).c_str());
    }
  }

  server.sendContent("]}");
}
