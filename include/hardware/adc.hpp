#pragma once

#include <esp_adc/adc_continuous.h>
#include <esp_http_server.h>

#include <cstdint>
#include <ebus/types.hpp>

class Adc {
 public:
  static constexpr size_t result_bytes = 2;

  bool begin();
  void stop();

  bool isRunning() const;
  static uint32_t effectivePerChannelSampleRate(uint32_t sample_rate,
                                                uint32_t channel_mask);
  bool streamRaw(const ebus::JsonChunkVisitor& visitor, uint32_t sample_rate,
                 uint32_t samples_per_channel, uint32_t channel_mask) const;

 private:
  bool startCapture() const;
  void stopCapture() const;
  bool configureController(uint32_t sample_rate, uint32_t channel_mask) const;
  static void logError(const char* stage, int err);

  static constexpr size_t dma_store_buffer_bytes_ = 4 * 1024;
  static constexpr size_t raw_frame_bytes_ = 1024;
  static constexpr size_t raw_http_chunk_bytes_ = 4096;
  static constexpr size_t dma_sample_bytes_ = 4;

  mutable adc_continuous_handle_t adc_handle_ = nullptr;
  mutable adc_digi_pattern_config_t adc_pattern_[5] = {};

  // Pre-allocated buffers to prevent stack overflow and heap fragmentation
  mutable uint8_t dma_buffer_[raw_frame_bytes_] = {};
  mutable uint8_t tx_buffer_[raw_http_chunk_bytes_] = {};
  mutable adc_continuous_data_t
      parsed_buffer_[raw_frame_bytes_ / dma_sample_bytes_];

  bool configured_ = false;
  mutable bool capturing_ = false;
};

extern Adc adc;
