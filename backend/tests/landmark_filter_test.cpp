#include <cassert>
#include <cmath>

#include "dualview/hand_tracker.hpp"
using namespace dualview;
namespace {

struct Rig {
  CameraIntrinsics intrinsics[2];
  StereoSolution stereo;
  CameraFrame frames[2];
  double baseline = .12;
  Rig() {
    for (int i = 0; i < 2; ++i) {
      auto& profile = intrinsics[i];
      profile.camera_id = i == 0 ? "laptop" : "phone";
      profile.width = 640;
      profile.height = 480;
      profile.camera_matrix = {550, 0, 320, 0, 550, 240, 0, 0, 1};
      profile.distortion_coefficients = cv::Mat::zeros(1, 5, CV_64F);
      profile.usable_for_metric_calibration = true;
      frames[i].image = cv::Mat::zeros(480, 640, CV_8UC3);
    }
    stereo.calibrated = true;
    stereo.translation_phone_from_laptop_m = {-baseline, 0, 0};
  }
  // Projects a hand centred at `centre` into both views.
  void observe(const cv::Vec3d& centre, std::vector<Hand> (&views)[2]) const {
    for (int i = 0; i < 2; ++i) {
      views[i].resize(1);
      views[i][0].confidence = .95f;
    }
    for (int j = 0; j < 21; ++j) {
      const cv::Vec3d p =
          centre + cv::Vec3d((j % 5 - 2) * .012, (j / 5 - 2) * .015, .002 * (j % 3));
      for (int camera = 0; camera < 2; ++camera)
        views[camera][0].landmarks[j] = {
            static_cast<float>(550 * (p[0] - camera * baseline) / p[2] + 320),
            static_cast<float>(550 * p[1] / p[2] + 240)};
    }
  }
};

// Drives a tracker along a constant-velocity path and returns the mean distance
// between the published pose and the truth, in millimetres.
double lag_mm(FilterStrategy strategy, bool compare, std::size_t* alternates = nullptr) {
  Rig rig;
  HandTracker tracker;
  tracker.configure(strategy, compare);
  auto now = Clock::now();
  double total = 0;
  int counted = 0;
  for (int step = 0; step < 40; ++step) {
    const cv::Vec3d centre(-.10 + .005 * step, 0, .60);
    std::vector<Hand> views[2];
    rig.observe(centre, views);
    for (auto& frame : rig.frames) frame.received = now;
    const auto result = tracker.track(rig.frames, views[0], views[1], rig.intrinsics, rig.stereo, 0,
                                      80, false, now);
    if (step >= 10 && result.hands[0].source == PoseSource::Measured) {
      assert(result.hands[0].filtered.size() == 21);
      total += 1000 * cv::norm(result.hands[0].filtered[0] - (centre + cv::Vec3d(-.024, -.030, 0)));
      ++counted;
      if (alternates) *alternates = result.hands[0].alternates.size();
    }
    now += std::chrono::milliseconds(33);
  }
  assert(counted > 0);
  return total / counted;
}

}  // namespace

int main() {
  // Default configuration must remain the original position-only blend.
  {
    HandTracker tracker;
    assert(tracker.strategy() == FilterStrategy::AlphaOnly);
    assert(!tracker.comparing());
  }

  // Strategy names round-trip, and unknown names are rejected rather than
  // silently falling back to a different estimator.
  {
    assert(parse_filter_strategy("alpha") == FilterStrategy::AlphaOnly);
    assert(parse_filter_strategy("alpha_beta") == FilterStrategy::AlphaBeta);
    assert(parse_filter_strategy("kalman") == FilterStrategy::Kalman);
    bool rejected = false;
    try {
      parse_filter_strategy("ekf");
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    assert(rejected);
    assert(std::string(filter_strategy_name(FilterStrategy::Kalman)) == "kalman");
  }

  // The velocity feedforward is the point of the beta term: on steady motion
  // both new estimators must track with less lag than the position-only blend.
  const double alpha_lag = lag_mm(FilterStrategy::AlphaOnly, false);
  const double alpha_beta_lag = lag_mm(FilterStrategy::AlphaBeta, false);
  const double kalman_lag = lag_mm(FilterStrategy::Kalman, false);
  assert(alpha_beta_lag < alpha_lag);
  assert(kalman_lag < alpha_lag);

  // Comparison mode reports the estimators that are not publishing, and must
  // not disturb the pose the selected estimator produces.
  {
    std::size_t alternates = 0;
    const double compared = lag_mm(FilterStrategy::AlphaOnly, true, &alternates);
    assert(alternates == 2);
    assert(std::abs(compared - alpha_lag) < 1e-9);
  }

  // A shadow estimator is carried through telemetry with a finite divergence,
  // and the published pose keeps its own strategy's output.
  {
    Rig rig;
    HandTracker tracker;
    tracker.configure(FilterStrategy::AlphaOnly, true);
    auto now = Clock::now();
    TrackingResult result;
    for (int step = 0; step < 12; ++step) {
      std::vector<Hand> views[2];
      rig.observe({-.05 + .006 * step, 0, .60}, views);
      for (auto& frame : rig.frames) frame.received = now;
      result = tracker.track(rig.frames, views[0], views[1], rig.intrinsics, rig.stereo, 0, 80,
                             false, now);
      now += std::chrono::milliseconds(33);
    }
    const auto json = tracking_json(result);
    assert(json["filter_alternates"].is_array());
    assert(json["filter_alternates"].size() == 2);
    for (const auto& alternate : json["filter_alternates"]) {
      assert(alternate["strategy"] != "alpha");
      assert(alternate["rms_delta_mm"].get<double>() >= 0);
      assert(std::isfinite(alternate["rms_delta_mm"].get<double>()));
      assert(alternate["filtered_points_m"].size() == 21);
    }
    const auto comparison = filter_comparison_json(result);
    assert(comparison.contains("primary_rms_delta_mm"));
    assert(comparison["primary_rms_delta_mm"].contains("kalman"));
    assert(comparison["primary_rms_delta_mm"].contains("alpha_beta"));
  }

  // Without comparison enabled nothing extra is computed or serialized.
  {
    Rig rig;
    HandTracker tracker;
    const auto now = Clock::now();
    std::vector<Hand> views[2];
    rig.observe({0, 0, .60}, views);
    for (auto& frame : rig.frames) frame.received = now;
    const auto result = tracker.track(rig.frames, views[0], views[1], rig.intrinsics, rig.stereo, 0,
                                      80, false, now);
    assert(result.hands[0].alternates.empty());
    assert(tracking_json(result)["filter_alternates"].is_null());
    assert(filter_comparison_json(result).is_null());
  }

  // Every estimator resets on a discontinuity rather than smoothing across a
  // jump that the gate has already rejected as a different track.
  {
    Rig rig;
    HandTracker tracker;
    tracker.configure(FilterStrategy::Kalman, false);
    auto now = Clock::now();
    std::vector<Hand> views[2];
    for (int step = 0; step < 6; ++step) {
      rig.observe({0, 0, .60}, views);
      for (auto& frame : rig.frames) frame.received = now;
      tracker.track(rig.frames, views[0], views[1], rig.intrinsics, rig.stereo, 0, 80, false, now);
      now += std::chrono::milliseconds(33);
    }
    const cv::Vec3d jumped(.45, 0, .60);  // Far beyond the 12 cm continuity gate.
    rig.observe(jumped, views);
    for (auto& frame : rig.frames) frame.received = now;
    const auto result = tracker.track(rig.frames, views[0], views[1], rig.intrinsics, rig.stereo, 0,
                                      80, false, now);
    assert(result.hands[0].source == PoseSource::Measured);
    // A filter that smoothed across the jump would still sit near the old pose.
    assert(cv::norm(result.hands[0].filtered[0] - result.hands[0].raw[0]) < 1e-6);
  }

  return 0;
}
