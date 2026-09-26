#pragma once
#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>

namespace dualview {
// Four-timestamp exchange. All times are milliseconds; remote times use Unix epoch.
// Minimum RTT limits queueing bias, but cannot establish symmetric network delay.
class ClockSync {
 public:
  struct Sample {
    double at, offset, rtt;
  };
  bool add(double sent, double received, double remote_received, double remote_sent) {
    if (!std::isfinite(sent) || !std::isfinite(received) || !std::isfinite(remote_received) ||
        !std::isfinite(remote_sent))
      return false;
    const double rtt = received - sent - (remote_sent - remote_received);
    if (received < sent || remote_sent < remote_received || rtt < 0 || rtt > 250 ||
        received - sent > 1000)
      return false;
    const double offset = ((remote_received - sent) + (remote_sent - received)) / 2;
    if (!samples.empty() && std::abs(offset - samples.back().offset) > 250) samples.clear();
    samples.push_back({received, offset, rtt});
    while (samples.size() > 8) samples.pop_front();
    return true;
  }
  std::optional<Sample> best(double now) const {
    std::optional<Sample> result;
    int count = 0;
    for (const auto& sample : samples) {
      if (now < sample.at || now - sample.at > 10000) continue;
      ++count;
      if (!result || sample.rtt < result->rtt) result = sample;
    }
    return count >= 3 ? result : std::nullopt;
  }

 private:
  std::deque<Sample> samples;
};
}  // namespace dualview
