#include "adc_stub_state.hpp"
#include "hardware/adc.hpp"

namespace HostAdcStub {

State& state() {
  static State state;
  return state;
}

void reset() { state() = State{}; }

}  // namespace HostAdcStub

bool Adc::begin() {
  auto& stub_state = HostAdcStub::state();
  ++stub_state.begin_calls;
  stub_state.running = stub_state.begin_result;
  return stub_state.begin_result;
}

void Adc::stop() {
  auto& stub_state = HostAdcStub::state();
  ++stub_state.stop_calls;
  stub_state.running = false;
}

bool Adc::isRunning() const { return HostAdcStub::state().running; }

uint32_t Adc::effectivePerChannelSampleRate(uint32_t sample_rate,
                                            uint32_t channel_mask) {
  (void)sample_rate;
  (void)channel_mask;
  return HostAdcStub::state().effective_sample_rate;
}
bool Adc::streamRaw(const ebus::JsonChunkVisitor& visitor, uint32_t sample_rate,
                    uint32_t samples_per_channel, uint32_t channel_mask) const {
  auto& stub_state = HostAdcStub::state();
  ++stub_state.stream_calls;
  stub_state.stream_sample_rate = sample_rate;
  stub_state.stream_samples_per_channel = samples_per_channel;
  stub_state.stream_channel_mask = channel_mask;
  if (visitor) {
    visitor(std::string_view("\x01\x02\x03\x04", 4));
  }
  return stub_state.stream_result;
}

bool Adc::startCapture() const { return true; }
void Adc::stopCapture() const {}
bool Adc::configureController(uint32_t sample_rate,
                              uint32_t channel_mask) const {
  (void)sample_rate;
  (void)channel_mask;
  return true;
}
void Adc::logError(const char* stage, int err) {
  (void)stage;
  (void)err;
}

Adc adc;
