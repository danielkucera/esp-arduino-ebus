#pragma once

#include <esp_http_server.h>

#include <cstdint>
#include <array>
#include <string>
#include <vector>

class Adc {
 public:
  static constexpr size_t SAMPLE_BUFFER_BYTES = 10 * 1024;
  static constexpr size_t DMA_STORE_BUFFER_BYTES = 32 * 1024;
  static constexpr size_t RESULT_BYTES = 4;

  bool begin();
  void stop();

  bool isRunning() const;
  bool streamJson(httpd_req_t* req, uint32_t sampleRate,
                  uint32_t samplesPerChannel, uint32_t channelMask) const;
  bool streamRaw(httpd_req_t* req, uint32_t sampleRate,
                 uint32_t samplesPerChannel, uint32_t channelMask) const;
  const std::string getJson(uint32_t sampleRate, uint32_t samplesPerChannel,
                            uint32_t channelMask) const;

 private:
  bool startCapture() const;
  void stopCapture() const;
  bool configureController(uint32_t sampleRate, uint32_t channelMask) const;
  bool collectSamples(std::array<std::vector<uint16_t>, 5>& channels,
                      uint32_t sampleRate, uint32_t samplesPerChannel,
                      uint32_t channelMask) const;
  void logError(const char* stage, int err) const;
  bool shouldLogNow() const;

  bool configured = false;
  mutable bool capturing = false;
  mutable uint32_t lastErrorLogMs = 0;
};

extern Adc adc;
