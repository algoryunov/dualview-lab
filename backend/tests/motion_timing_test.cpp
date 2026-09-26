#include "dualview/motion_timing.hpp"

#include <cassert>
using namespace dualview;
double motion(double t) {
  return .5 + .14 * std::sin(t * .0031) + .06 * std::sin(t * .0067) + .025 * std::sin(t * .0103);
}
int main() {
  std::deque<MotionSample> samples[2];
  const auto fill = [&](int delay) {
    samples[0].clear();
    samples[1].clear();
    for (int t = 0; t < 20000; t += 67) samples[0].push_back({double(t), motion(t)});
    for (int t = 15; t < 20000; t += 69)
      samples[1].push_back({double(t), .1 + .8 * motion(t - delay)});
  };
  for (int delay : {45, 90, -65}) {
    fill(delay);
    auto result = estimate_motion_delay(samples);
    assert(result.accepted);
    assert(std::abs(result.offset_ms - delay) <= 10);
  }
  fill(0);
  assert(!estimate_motion_delay(samples).accepted);
  fill(150);
  assert(!estimate_motion_delay(samples).accepted);
  fill(60);
  for (auto& sample : samples[1]) sample.x = .5;
  assert(!estimate_motion_delay(samples).accepted);
  fill(60);
  for (auto& sample : samples[1])
    sample.x = motion(sample.time_ms - (sample.time_ms < 10000 ? 30 : 120));
  assert(!estimate_motion_delay(samples).accepted);
  samples[1].resize(20);
  assert(!estimate_motion_delay(samples).accepted);
}
