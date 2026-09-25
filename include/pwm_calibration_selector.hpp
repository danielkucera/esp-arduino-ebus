#pragma once

#include <cstdint>

struct PwmCalibrationMetrics {
  uint32_t symbols;
  uint32_t syn;
  uint32_t samples;
  uint32_t high_samples;
  uint32_t transitions;
};

inline bool pwmSignalIsStable(const PwmCalibrationMetrics& metrics) {
  if (metrics.high_samples > metrics.samples) return false;
  const uint32_t low_samples = metrics.samples - metrics.high_samples;
  return metrics.syn >= 2 && metrics.symbols >= metrics.syn &&
         metrics.transitions >= 4 && metrics.samples >= 100 &&
         static_cast<uint64_t>(metrics.high_samples) * 1000 >=
             metrics.samples &&
         static_cast<uint64_t>(low_samples) * 1000 >= metrics.samples;
}

class PwmCalibrationSweep {
 public:
  static constexpr uint8_t first = 1;
  static constexpr uint8_t last = 255;
  static constexpr uint8_t step = 2;
  static constexpr uint8_t minimum_stable_candidates = 3;

  void begin(uint8_t original) {
    original_ = original;
    candidate_ = first;
    current_start_ = 0;
    best_start_ = 0;
    best_end_ = 0;
    finished_ = false;
  }

  uint8_t candidate() const { return candidate_; }
  bool finished() const { return finished_; }
  bool foundStableBand() const { return best_start_ != 0; }
  uint8_t stableStart() const { return best_start_; }
  uint8_t stableEnd() const { return best_end_; }
  uint8_t selected() const {
    return foundStableBand() ? testedMidpoint(best_start_, best_end_, original_)
                             : original_;
  }

  bool record(bool stable) {
    if (finished_) return true;
    if (stable) {
      if (current_start_ == 0) current_start_ = candidate_;
    } else {
      closeRun(static_cast<uint8_t>(candidate_ - step));
    }
    if (candidate_ == last) {
      closeRun(last);
      finished_ = true;
      return true;
    }
    candidate_ = static_cast<uint8_t>(candidate_ + step);
    return false;
  }

 private:
  static uint8_t testedMidpoint(uint8_t start, uint8_t end,
                                uint8_t reference) {
    const uint8_t center = static_cast<uint8_t>(start + (end - start) / 2);
    if ((center & 1U) != 0) return center;
    const uint8_t below = static_cast<uint8_t>(center - 1);
    const uint8_t above = static_cast<uint8_t>(center + 1);
    return reference > center ? above : below;
  }

  void closeRun(uint8_t end) {
    if (current_start_ == 0) return;
    const uint16_t candidate_count =
        static_cast<uint16_t>((end - current_start_) / step) + 1;
    if (candidate_count < minimum_stable_candidates) {
      current_start_ = 0;
      return;
    }
    const uint16_t width = end - current_start_;
    const uint16_t best_width = best_end_ - best_start_;
    const uint8_t selected = testedMidpoint(current_start_, end, original_);
    const int distance = selected > original_ ? selected - original_
                                               : original_ - selected;
    const uint8_t best_selected =
        testedMidpoint(best_start_, best_end_, original_);
    const int best_distance =
        !foundStableBand()
            ? 256
            : best_selected > original_ ? best_selected - original_
                                        : original_ - best_selected;
    if (!foundStableBand() || width > best_width ||
        (width == best_width && distance < best_distance)) {
      best_start_ = current_start_;
      best_end_ = end;
    }
    current_start_ = 0;
  }

  uint8_t original_ = 130;
  uint8_t candidate_ = first;
  uint8_t current_start_ = 0;
  uint8_t best_start_ = 0;
  uint8_t best_end_ = 0;
  bool finished_ = false;
};
