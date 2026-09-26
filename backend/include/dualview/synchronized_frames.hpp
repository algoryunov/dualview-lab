#pragma once
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <opencv2/core.hpp>
#include <optional>

namespace dualview {
using Clock = std::chrono::steady_clock;
struct CameraFrame {
  cv::Mat image;
  Clock::time_point received{};
  std::uint64_t sequence{};
  // Receiver media timestamps are NOT sensor exposure timestamps.
  std::optional<double> media_pts_ms;
  std::optional<double> receiver_lateness_ms;
};
inline Clock::time_point pairing_time(const CameraFrame& frame, int camera, int phone_offset_ms) {
  return frame.received - std::chrono::milliseconds(camera == 1 ? phone_offset_ms : 0);
}
inline void remember_frame(std::deque<CameraFrame>& history, const CameraFrame& frame) {
  history.push_back(frame);
  while (history.size() > 8 || (!history.empty() && frame.received - history.front().received >
                                                        std::chrono::milliseconds(300)))
    history.pop_front();
}
// Anchor the newest frame of the slower stream; find its closest partner.
// Never wait for more frames or return a pair older than the latency budget.
inline std::optional<std::array<CameraFrame, 2>> closest_pair(
    const std::deque<CameraFrame> (&history)[2], Clock::time_point now, int phone_offset_ms,
    int max_error_ms) {
  if (history[0].empty() || history[1].empty()) return std::nullopt;
  const int anchor = pairing_time(history[0].back(), 0, phone_offset_ms) <=
                             pairing_time(history[1].back(), 1, phone_offset_ms)
                         ? 0
                         : 1;
  const auto& fixed = history[anchor].back();
  const CameraFrame* best = nullptr;
  double best_error = max_error_ms + .001;
  for (const auto& candidate : history[1 - anchor]) {
    if (candidate.image.empty() ||
        now - pairing_time(candidate, 1 - anchor, phone_offset_ms) >
            std::chrono::milliseconds(250) ||
        now - pairing_time(fixed, anchor, phone_offset_ms) > std::chrono::milliseconds(250) ||
        fixed.image.empty())
      continue;
    const double error = std::abs(std::chrono::duration<double, std::milli>(
                                      pairing_time(fixed, anchor, phone_offset_ms) -
                                      pairing_time(candidate, 1 - anchor, phone_offset_ms))
                                      .count());
    if (error <= best_error) {
      best = &candidate;
      best_error = error;
    }
  }
  if (!best) return std::nullopt;
  std::array<CameraFrame, 2> result;
  result[anchor] = fixed;
  result[1 - anchor] = *best;
  return result;
}
// Capture and preview never acquire the algorithm/state lock.
class FrameStore {
 public:
  struct Metrics {
    std::size_t depth[2]{};
    std::uint64_t evicted[2]{};
    double max_lock_wait_us = 0;
  };
  std::uint64_t phone_generation() const {
    std::lock_guard lock(mutex_);
    return phone_generation_;
  }
  void publish(int camera, CameraFrame frame,
               std::optional<std::uint64_t> expected_generation = {}) {
    const auto started = Clock::now();
    std::lock_guard lock(mutex_);
    metrics_.max_lock_wait_us =
        std::max(metrics_.max_lock_wait_us,
                 std::chrono::duration<double, std::micro>(Clock::now() - started).count());
    if (camera == 1 && expected_generation && *expected_generation != phone_generation_) return;
    const auto before = history_[camera].size();
    frame.sequence = latest_[camera].sequence + 1;
    latest_[camera] = std::move(frame);
    remember_frame(history_[camera], latest_[camera]);
    metrics_.depth[camera] = history_[camera].size();
    metrics_.evicted[camera] += before + 1 - history_[camera].size();
  }
  void clear_phone() {
    std::lock_guard lock(mutex_);
    ++phone_generation_;
    latest_[1].image.release();
    history_[1].clear();
    metrics_.depth[1] = 0;
  }
  Metrics metrics() const {
    std::lock_guard lock(mutex_);
    return metrics_;
  }
  std::array<CameraFrame, 2> latest() const {
    std::lock_guard lock(mutex_);
    return latest_;
  }
  std::array<CameraFrame, 2> select(Clock::time_point now, int offset, int error,
                                    const std::uint64_t (&previous)[2]) const {
    std::lock_guard lock(mutex_);
    if (const auto pair = closest_pair(history_, now, offset, error))
      if ((*pair)[0].sequence >= previous[0] && (*pair)[1].sequence >= previous[1]) return *pair;
    return latest_;
  }

 private:
  mutable std::mutex mutex_;
  Metrics metrics_;
  std::uint64_t phone_generation_ = 0;
  std::array<CameraFrame, 2> latest_;
  std::deque<CameraFrame> history_[2];
};
// Cache is owned by the processing thread. Epoch invalidates entries on reconnect.
template <class Value>
class DetectionCache {
  std::uint64_t epoch_ = 0;
  std::deque<std::pair<std::uint64_t, Value>> entries_;

 public:
  template <class Detect>
  Value get(std::uint64_t sequence, std::uint64_t epoch, Detect detect, bool& hit) {
    if (epoch_ != epoch) {
      entries_.clear();
      epoch_ = epoch;
    }
    for (const auto& entry : entries_) {
      if (entry.first == sequence) {
        hit = true;
        return entry.second;
      }
    }
    hit = false;
    auto value = detect();
    entries_.emplace_back(sequence, value);
    while (entries_.size() > 8) entries_.pop_front();
    return value;
  }
};
}  // namespace dualview
