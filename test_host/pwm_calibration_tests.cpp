#include <cstdint>
#include <iostream>

#include "pwm_calibration_selector.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  ++failures;
}

void testSignalStability() {
  check(pwmSignalIsStable({100, 10, 1000, 900, 20}),
        "representative signal should be stable");
  check(!pwmSignalIsStable({100, 1, 1000, 900, 20}),
        "one SYN is insufficient");
  check(!pwmSignalIsStable({100, 10, 1000, 1000, 20}),
        "both logic levels are required");
  check(!pwmSignalIsStable({100, 10, 100, 101, 20}),
        "high samples cannot exceed total samples");
}

void testWidestRangeSelection() {
  PwmCalibrationSweep sweep;
  sweep.begin(130);
  while (!sweep.finished()) {
    const uint8_t candidate = sweep.candidate();
    sweep.record(candidate >= 51 && candidate <= 101);
  }
  check(sweep.foundStableBand(), "stable range should be found");
  check(sweep.stableStart() == 51, "stable range should start at 51");
  check(sweep.stableEnd() == 101, "stable range should end at 101");
  check(sweep.selected() == 77, "selection should be a tested midpoint");
}

void testEqualRangeTieBreak() {
  PwmCalibrationSweep sweep;
  sweep.begin(170);
  while (!sweep.finished()) {
    const uint8_t candidate = sweep.candidate();
    const bool stable = (candidate >= 21 && candidate <= 41) ||
                        (candidate >= 151 && candidate <= 171);
    sweep.record(stable);
  }
  check(sweep.stableStart() == 151,
        "tie should prefer range closest to original");
  check(sweep.stableEnd() == 171, "selected range should end at 171");
  check(sweep.selected() == 161, "selected midpoint should be 161");
}

void testInvalidRanges() {
  PwmCalibrationSweep sweep;
  sweep.begin(130);
  while (!sweep.finished()) sweep.record(false);
  check(!sweep.foundStableBand(), "empty sweep should have no stable range");
  check(sweep.selected() == 130, "empty sweep should retain original");

  sweep.begin(130);
  while (!sweep.finished()) {
    const uint8_t candidate = sweep.candidate();
    sweep.record(candidate == 51 || candidate == 101);
  }
  check(!sweep.foundStableBand(), "isolated candidates should be rejected");
  check(sweep.selected() == 130,
        "isolated candidates should retain original");
}

}  // namespace

int main() {
  testSignalStability();
  testWidestRangeSelection();
  testEqualRangeTieBreak();
  testInvalidRanges();
  if (failures == 0) std::cout << "PWM calibration tests passed\n";
  return failures == 0 ? 0 : 1;
}
