#pragma once

#include <cstdint>

namespace HostAdcStub {

struct State {
  bool begin_result = true;
  bool running = false;
  bool stream_result = true;
  uint32_t effective_sample_rate = 30000;
  uint32_t stream_sample_rate = 0;
  uint32_t stream_samples_per_channel = 0;
  uint32_t stream_channel_mask = 0;
  uint32_t begin_calls = 0;
  uint32_t stop_calls = 0;
  uint32_t stream_calls = 0;
};

State& state();
void reset();

}  // namespace HostAdcStub
