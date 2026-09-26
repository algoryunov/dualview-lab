#pragma once
#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <vector>
namespace dualview {
struct MotionSample {
  double time_ms, x;
};
struct TimingEstimate {
  bool accepted = false;
  int offset_ms = 0;
  double correlation = 0, baseline_correlation = 0;
  std::string reason = "Need at least eight seconds of one-hand motion";
};
// Positive offset means the phone observes the same motion later at the receiver.
// This estimates a relative visual delay, not sensor clock synchronization.
inline TimingEstimate estimate_motion_delay(const std::deque<MotionSample> (&samples)[2]) {
  TimingEstimate result;
  if (samples[0].size() < 80 || samples[1].size() < 80) return result;
  const double start = std::max(samples[0].front().time_ms, samples[1].front().time_ms) + 150;
  const double end = std::min(samples[0].back().time_ms, samples[1].back().time_ms) - 150;
  if (end - start < 8000) return result;
  const auto score = [&](int offset, double lo, double hi) {
    std::vector<double> a, b;
    for (const auto& s : samples[0]) {
      if (s.time_ms < lo || s.time_ms > hi) continue;
      const double target = s.time_ms + offset;
      const auto next = std::lower_bound(samples[1].begin(), samples[1].end(), target,
                                         [](const auto& v, double t) { return v.time_ms < t; });
      if (next == samples[1].begin() || next == samples[1].end()) continue;
      const auto prev = std::prev(next);
      const double gap = next->time_ms - prev->time_ms;
      if (gap <= 0 || gap > 150) continue;
      a.push_back(s.x);
      b.push_back(prev->x + (next->x - prev->x) * (target - prev->time_ms) / gap);
    }
    if (a.size() < 35) return -1.0;
    double ma = 0, mb = 0;
    for (size_t i = 0; i < a.size(); ++i) {
      ma += a[i];
      mb += b[i];
    }
    ma /= a.size();
    mb /= b.size();
    double aa = 0, bb = 0, ab = 0;
    for (size_t i = 0; i < a.size(); ++i) {
      aa += (a[i] - ma) * (a[i] - ma);
      bb += (b[i] - mb) * (b[i] - mb);
      ab += (a[i] - ma) * (b[i] - mb);
    }
    if (std::sqrt(aa / a.size()) < .025 || std::sqrt(bb / b.size()) < .025) return -1.0;
    return std::abs(ab / std::sqrt(aa * bb));
  };
  const auto best = [&](double lo, double hi) {
    std::pair<int, double> winner{0, -1};
    for (int offset = -150; offset <= 150; offset += 5) {
      const auto value = score(offset, lo, hi);
      if (value > winner.second) winner = {offset, value};
    }
    return winner;
  };
  const auto all = best(start, end), first = best(start, (start + end) / 2),
             second = best((start + end) / 2, end);
  result.offset_ms = all.first;
  result.correlation = std::max(0.0, all.second);
  result.baseline_correlation = std::max(0.0, score(0, start, end));
  if (all.second < .95 || first.second < .9 || second.second < .9)
    result.reason = "Motion too small, obscured or different between views; use one open palm";
  else if (std::abs(all.first) >= 145)
    result.reason = "Estimated delay reaches search limit; check camera and network latency";
  else if (std::abs(first.first - second.first) > 25 || std::abs(first.first - all.first) > 20 ||
           std::abs(second.first - all.first) > 20)
    result.reason = "Delay differs between test halves; repeat with irregular side-to-side motion";
  else if (std::abs(all.first) < 15 || all.second - result.baseline_correlation < .002 ||
           1 - all.second > .8 * (1 - result.baseline_correlation))
    result.reason = "No reliable improvement over zero compensation";
  else {
    result.accepted = true;
    result.reason =
        "Consistent motion delay in both test halves; validate with a compensated movement test";
  }
  return result;
}
}  // namespace dualview
