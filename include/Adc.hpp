#pragma once

#include <esp_adc/adc_continuous.h>
#include <esp_http_server.h>

#include <cstdint>
#include <ebus/types.hpp>

class Adc {
 public:
  static constexpr size_t SAMPLE_BUFFER_BYTES = 10 * 1024;
  static constexpr size_t DMA_STORE_BUFFER_BYTES = 32 * 1024;
  static constexpr size_t RESULT_BYTES = 2;
  static constexpr size_t ADC_RAW_FRAME_BYTES = 1024;
  static constexpr size_t ADC_RAW_HTTP_CHUNK_BYTES = 4096;
  static constexpr size_t ADC_DMA_SAMPLE_BYTES = 4;

  bool begin();
  void stop();

  bool isRunning() const;
  uint32_t effectivePerChannelSampleRate(uint32_t sampleRate,
                                         uint32_t channelMask) const;
  bool streamRaw(const ebus::JsonChunkVisitor& visitor, uint32_t sampleRate,
                 uint32_t samplesPerChannel, uint32_t channelMask) const;

 private:
  bool startCapture() const;
  void stopCapture() const;
  bool configureController(uint32_t sampleRate, uint32_t channelMask) const;
  void logError(const char* stage, int err) const;

  mutable adc_continuous_handle_t adc_handle_ = nullptr;
  mutable adc_digi_pattern_config_t adc_pattern_[5] = {};

  // Pre-allocated buffers to prevent stack overflow and heap fragmentation
  mutable uint8_t dma_buffer_[ADC_RAW_FRAME_BYTES];
  mutable uint8_t tx_buffer_[ADC_RAW_HTTP_CHUNK_BYTES];
  mutable adc_continuous_data_t
      parsed_buffer_[ADC_RAW_FRAME_BYTES / ADC_DMA_SAMPLE_BYTES];

  bool configured = false;
  mutable bool capturing = false;
};

extern Adc adc;
