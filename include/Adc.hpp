#pragma once

#include <esp_http_server.h>

#include <cstdint>

class Adc {
 public:
  static constexpr size_t SAMPLE_BUFFER_BYTES = 10 * 1024;
  static constexpr size_t DMA_STORE_BUFFER_BYTES = 32 * 1024;
  static constexpr size_t RESULT_BYTES = 4;

  bool begin();
  void stop();

  bool isRunning() const;
  bool streamRaw(httpd_req_t* req, uint32_t sampleRate,
                 uint32_t samplesPerChannel, uint32_t channelMask) const;

 private:
  bool startCapture() const;
  void stopCapture() const;
  bool configureController(uint32_t sampleRate, uint32_t channelMask) const;
  void logError(const char* stage, int err) const;

  bool configured = false;
  mutable bool capturing = false;
};

extern Adc adc;
