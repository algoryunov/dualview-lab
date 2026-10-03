#pragma once
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>

#include "dualview/event_log.hpp"
#include "dualview/hand_tracker.hpp"

namespace dualview {
// Per-frame landmark recorder for offline estimator comparison.
//
// The ordinary tracking log is a status-change sampler: it writes at most once
// per second, carries no landmarks, and cannot show what a filter does between
// samples. This records every processed pair instead, with raw and filtered
// landmarks plus any shadow estimator's output, so a session can be replayed
// offline.
//
// It is opt-in and bounded: an empty path disables it entirely, and recording
// stops after a fixed duration so an unattended run cannot fill a disk.
class PoseCapture {
 public:
  PoseCapture(std::string path, int seconds)
      : enabled_(!path.empty()),
        limit_seconds_(seconds),
        log_(std::move(path), 64ull * 1024 * 1024) {}

  bool enabled() const { return enabled_ && !finished_; }

  // Written once, before any frame: everything needed to interpret the samples
  // and to re-run an estimator with the same geometry.
  void begin(const nlohmann::json& metadata) {
    if (!enabled()) return;
    started_ = Clock::now();
    nlohmann::json header = metadata;
    header["event"] = "capture_header";
    header["limit_seconds"] = limit_seconds_;
    header["schema"] = "dualview.pose_capture.v1";
    header["wall_clock_ms"] = wall_ms();
    log_.write(header);
    begun_ = true;
  }

  void record(const TrackingResult& result, bool processed_pair) {
    if (!enabled() || !begun_ || !processed_pair) return;
    const double elapsed = std::chrono::duration<double>(Clock::now() - started_).count();
    if (elapsed > limit_seconds_) {
      log_.write({{"event", "capture_complete"},
                  {"reason", "duration_limit"},
                  {"samples", samples_},
                  {"elapsed_s", elapsed}});
      finished_ = true;
      return;
    }
    nlohmann::json sample = {
        {"event", "pose_sample"},
        {"t_s", round_to(elapsed, 4)},
        {"wall_clock_ms", wall_ms()},
        {"reason", result.reason.empty() ? nlohmann::json(nullptr) : nlohmann::json(result.reason)},
        {"timing_error_ms", optional(result.timing_error_ms)},
        {"arrival_delta_ms", optional(result.arrival_delta_ms)},
        {"candidate_residual_px", optional(result.candidate_residual_px)}};
    for (int i = 0; i < 2; ++i)
      sample[i == 0 ? "primary" : "secondary"] = hand_json(result.hands[i]);
    log_.write(sample);
    ++samples_;
  }

  std::uint64_t samples() const { return samples_; }

 private:
  static double round_to(double value, int decimals) {
    const double scale = std::pow(10, decimals);
    return std::round(value * scale) / scale;
  }
  // Landmarks are rounded to 10 micrometres: far below any real measurement
  // difference, and it roughly halves the file.
  static nlohmann::json pose(const Pose& points) {
    if (points.empty()) return nullptr;
    nlohmann::json out = nlohmann::json::array();
    for (const auto& p : points)
      out.push_back({round_to(p[0], 5), round_to(p[1], 5), round_to(p[2], 5)});
    return out;
  }
  static nlohmann::json optional(std::optional<double> value) {
    return value ? nlohmann::json(round_to(*value, 3)) : nlohmann::json(nullptr);
  }
  static nlohmann::json hand_json(const TrackedHand& hand) {
    if (hand.source == PoseSource::Missing && hand.raw.empty() && hand.filtered.empty())
      return nullptr;
    nlohmann::json out = {{"source", hand.source == PoseSource::Measured    ? "measured"
                                     : hand.source == PoseSource::Predicted ? "predicted"
                                                                            : "missing"},
                          {"confidence", round_to(hand.confidence, 4)},
                          {"hold_age_ms", optional(hand.hold_age_ms)},
                          {"raw_points_m", pose(hand.raw)},
                          {"filtered_points_m", pose(hand.filtered)}};
    if (!hand.residuals.empty()) {
      nlohmann::json residuals = nlohmann::json::array();
      for (const auto value : hand.residuals) residuals.push_back(round_to(value, 3));
      out["reprojection_residuals_px"] = residuals;
    }
    if (!hand.alternates.empty()) {
      nlohmann::json alternates = nlohmann::json::array();
      for (const auto& alternate : hand.alternates)
        alternates.push_back({{"strategy", alternate.strategy},
                              {"rms_delta_mm", round_to(alternate.rms_delta_mm, 4)},
                              {"filtered_points_m", pose(alternate.filtered)}});
      out["filter_alternates"] = alternates;
    }
    return out;
  }
  static std::int64_t wall_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  bool enabled_;
  int limit_seconds_;
  EventLog log_;
  Clock::time_point started_{};
  bool begun_ = false;
  bool finished_ = false;
  std::uint64_t samples_ = 0;
};
}  // namespace dualview
