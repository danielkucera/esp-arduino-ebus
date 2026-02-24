#pragma once

#include <array>
#include <cstdint>
#include <string>

class WebServer;

class Adc {
 public:
  static constexpr size_t SAMPLE_BUFFER_BYTES = 10 * 1024;
  static constexpr size_t DMA_STORE_BUFFER_BYTES = 4 * 1024;
  static constexpr size_t RESULT_BYTES = 4;

  bool begin();
  void stop();

  const bool isRunning() const;
  const std::string getJson() const;
  void streamJson(WebServer& server) const;

 private:
  static void captureTask(void* param);
  void pushRawBytes(const uint8_t* data, size_t len);
  void logError(const char* stage, int err) const;
  void recoverFromInvalidState();
  bool shouldLogNow() const;

  bool running = false;

  std::array<uint8_t, SAMPLE_BUFFER_BYTES> rawBuffer = {};
  size_t rawWriteIndex = 0;
  size_t rawBytesFilled = 0;
  uint32_t totalFrames = 0;

  void* bufferMutex = nullptr;
  void* captureTaskHandle = nullptr;
  mutable uint32_t lastErrorLogMs = 0;
};

extern Adc adc;
