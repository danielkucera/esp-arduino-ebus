#include "bridge_timing.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

int main() {
  // Captured falling edges of SYN at 2400 baud, including timer rollover.
  for (uint32_t base : {10000U, UINT32_MAX - 2000U}) {
    const uint32_t edges[4] = {base, base + 1250, base + 2083, base + 2917};
    uint32_t start = 0;
    for (uint32_t latency = 0; latency <= 2000; ++latency) {
      const bool fresh = bridge::synStartBit(edges, 4, base + 3958 + latency, start);
      assert(fresh == (latency <= 498));
      if (fresh) assert(start == base);
    }
    for (uint32_t age = 0; age < 10000; ++age) {
      assert(bridge::inArbitrationWindow(base, base + age) ==
             (age >= 4300 && age <= 4456));
      assert(bridge::canCommitArbitration(base, base + age) ==
             (age >= 4300 && age <= 4406));
    }
    assert(bridge::waitForArbitration(base, base + 3958) == 342);
    assert(bridge::waitForArbitration(base, base + 4400) == 0);
    assert(!bridge::synStartBit(edges, 3, base + 4000, start));
    assert(!bridge::synStartBit(edges, 4, base + 1000, start));
    // A new start bit replaces the oldest edge: never arbitrate in a frame.
    const uint32_t next[4] = {base + 1250, base + 2083, base + 2917, base + 4167};
    assert(!bridge::synStartBit(next, 4, base + 4300, start));
    // Incorrect 2/2/2 bit spacing would select a data bit as the start bit.
    const uint32_t wrong[4] = {base, base + 833, base + 1667, base + 2500};
    assert(!bridge::synStartBit(wrong, 4, base + 4000, start));
  }
  // Realistic independent ISR jitter; do not synthesize timing from read time.
  const uint32_t jitter[4] = {10005, 11263, 12081, 12938};
  uint32_t start = 0;
  assert(bridge::synStartBit(jitter, 4, 14010, start));
  assert(start == 10005);
  assert(!bridge::inArbitrationWindow(start, start + 4600));
  puts("Bridge SYN timing tests passed");
}
