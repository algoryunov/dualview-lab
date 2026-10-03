#pragma once
#include <nlohmann/json.hpp>

#include "dualview/calibration.hpp"
#include "dualview/hand_pipeline.hpp"
#include "dualview/landmark_filter.hpp"
#include "dualview/synchronized_frames.hpp"
namespace dualview {
enum class PoseSource { Missing, Measured, Predicted };
// A shadow estimator's output for the same frame, carried only for comparison.
// It never contributes to the published pose.
struct AlternatePose {
  std::string strategy;
  Pose filtered;
  double rms_delta_mm = 0;
};
struct TrackedHand {
  Pose raw, filtered;
  std::vector<double> residuals;
  double confidence = 0;
  PoseSource source = PoseSource::Missing;
  std::optional<double> hold_age_ms;
  std::vector<AlternatePose> alternates;
};
struct TrackingResult {
  TrackedHand hands[2];
  std::vector<std::vector<cv::Point2d>> normalized[2];
  std::vector<float> confidences[2];
  std::optional<double> timing_error_ms, arrival_delta_ms, candidate_residual_px;
  std::string reason = "hand_lost_in_one_or_both_views";
  bool tracked() const { return !hands[0].filtered.empty(); }
  bool holding() const {
    return hands[0].source == PoseSource::Predicted || hands[1].source == PoseSource::Predicted;
  }
};
nlohmann::json tracking_json(const TrackingResult& result);
nlohmann::json filter_comparison_json(const TrackingResult& result);
class HandTracker {
 public:
  static constexpr double pose_grace_ms = 500;
  // Selects the estimator that produces the published pose, and whether the
  // others run alongside it for comparison. Defaults reproduce the original
  // behaviour exactly.
  void configure(FilterStrategy strategy, bool compare, const FilterTuning& tuning = {}) {
    strategy_ = strategy;
    compare_ = compare;
    tuning_ = tuning;
  }
  FilterStrategy strategy() const { return strategy_; }
  bool comparing() const { return compare_; }
  TrackingResult track(const CameraFrame (&frames)[2], const std::vector<Hand>& laptop,
                       const std::vector<Hand>& phone, const CameraIntrinsics (&intrinsics)[2],
                       const StereoSolution& stereo, int phone_offset_ms, int max_pair_error_ms,
                       bool calibrating = false, Clock::time_point now = Clock::now());
  bool expire(TrackingResult& result, Clock::time_point now = Clock::now());
  void reset() {
    for (auto& hand : stable_) hand = {};
  }

 private:
  struct StableHand {
    Pose points;
    double confidence = 0;
    cv::Vec3d velocity{};
    Clock::time_point seen{};
    AlphaBetaFilter alpha_beta;
    KalmanFilter kalman;
  };
  StableHand stable_[2];
  FilterStrategy strategy_ = FilterStrategy::AlphaOnly;
  bool compare_ = false;
  FilterTuning tuning_;
};
}  // namespace dualview
