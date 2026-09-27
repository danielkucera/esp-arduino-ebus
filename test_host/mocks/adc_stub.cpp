#include "hardware/adc.hpp"

bool Adc::begin() { return true; }
void Adc::stop() {}
bool Adc::isRunning() const { return false; }
uint32_t Adc::effectivePerChannelSampleRate(uint32_t sampleRate,
                                            uint32_t channelMask) {
  (void)sampleRate;
  (void)channelMask;
  return 30000;
}
bool Adc::streamRaw(const ebus::JsonChunkVisitor& visitor, uint32_t sampleRate,
                    uint32_t samplesPerChannel, uint32_t channelMask) const {
  (void)sampleRate;
  (void)samplesPerChannel;
  (void)channelMask;
  if (visitor) {
    visitor(std::string_view("\x01\x02\x03\x04", 4));
  }
  return true;
}

bool Adc::startCapture() const { return true; }
void Adc::stopCapture() const {}
bool Adc::configureController(uint32_t sampleRate, uint32_t channelMask) const {
  (void)sampleRate;
  (void)channelMask;
  return true;
}
void Adc::logError(const char* stage, int err) {
  (void)stage;
  (void)err;
}

Adc adc;
