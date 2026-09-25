#pragma once

#include <cstddef>
#include <cstdint>

namespace bridge {

// eBUS is 2400 baud, 8N1. Permission to transmit is measured from the SYN
// start bit, not from the time a task happens to drain the UART buffer.
inline constexpr uint32_t arbitration_earliest_us = 4300;
inline constexpr uint32_t arbitration_latest_us = 4456;
// Leave time for GPIO/FIFO checks and the hardware transmitter to start.
inline constexpr uint32_t transmit_margin_us = 50;
inline constexpr uint32_t stop_sample_us = 3958;
inline constexpr uint32_t edge_tolerance_us = 100;

inline bool nearInterval(uint32_t value, uint32_t expected) {
  return value >= expected - edge_tolerance_us &&
         value <= expected + edge_tolerance_us;
}

// Falling edges of 0xAA (LSB first) are at start, bit 2, bit 4 and bit 6:
// offsets 0, 3, 5, 7 bit times. The first interval is THREE bit times because
// data bit 0 is low too. Do not guess from task arrival time if edges are lost.
inline bool synStartBit(const uint32_t (&edges)[4], size_t count,
                        uint32_t now, uint32_t& start) {
  if (count < 4 || !nearInterval(edges[1] - edges[0], 1250) ||
      !nearInterval(edges[2] - edges[1], 833) ||
      !nearInterval(edges[3] - edges[2], 833)) {
    return false;
  }
  const uint32_t age = now - edges[0];
  if (age < stop_sample_us - edge_tolerance_us ||
      age > arbitration_latest_us) {
    return false;
  }
  start = edges[0];
  return true;
}

inline bool inArbitrationWindow(uint32_t start, uint32_t now) {
  const uint32_t age = now - start;
  return age >= arbitration_earliest_us && age <= arbitration_latest_us;
}

inline bool canCommitArbitration(uint32_t start, uint32_t now) {
  return inArbitrationWindow(start, now) &&
         now - start <= arbitration_latest_us - transmit_margin_us;
}

inline uint32_t waitForArbitration(uint32_t start, uint32_t now) {
  const uint32_t age = now - start;
  return age < arbitration_earliest_us ? arbitration_earliest_us - age : 0;
}

}  // namespace bridge
