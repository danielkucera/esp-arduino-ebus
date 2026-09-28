#pragma once

#include <esp_adc/adc_continuous.h>
#include <esp_http_server.h>

#include <cstdint>
#include <ebus/types.hpp>

class Adc {
 public:
  static constexpr size_t sample_buffer_bytes = 10 * 1024;
  static constexpr size_t dma_store_buffer_bytes = 4 * 1024;
  static constexpr size_t result_bytes = 2;
  static constexpr size_t adc_raw_frame_bytes = 1024;
  static constexpr size_t adc_raw_http_chunk_bytes = 4096;
  static constexpr size_t adc_dma_sample_bytes = 4;

  bool begin();
  void stop();

  bool isRunning() const;
  static uint32_t effectivePerChannelSampleRate(uint32_t sampleRate,
                                                uint32_t channelMask);
  bool streamRaw(const ebus::JsonChunkVisitor& visitor, uint32_t sampleRate,
                 uint32_t samplesPerChannel, uint32_t channelMask) const;

 private:
  bool startCapture() const;
  void stopCapture() const;
  bool configureController(uint32_t sampleRate, uint32_t channelMask) const;
  static void logError(const char* stage, int err);

  mutable adc_continuous_handle_t adc_handle_ = nullptr;
  mutable adc_digi_pattern_config_t adc_pattern_[5] = {};

  // Pre-allocated buffers to prevent stack overflow and heap fragmentation
  mutable uint8_t dma_buffer_[adc_raw_frame_bytes] = {};
  mutable uint8_t tx_buffer_[adc_raw_http_chunk_bytes] = {};
  mutable adc_continuous_data_t
      parsed_buffer_[adc_raw_frame_bytes / adc_dma_sample_bytes];

  bool configured_ = false;
  mutable bool capturing_ = false;
};

extern Adc adc;
