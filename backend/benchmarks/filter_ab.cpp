// Paired A/B of the production alpha-beta filter against a constant-velocity
// Kalman filter with anisotropic measurement noise.
//
// Both arms consume the SAME measurement stream: a persistent HandTracker is
// driven over a synthetic trajectory, and each step yields
//   raw      = triangulated landmarks, unfiltered
//   filtered = the production filter (stereo_tracking.cpp)
// The Kalman arm is fed `raw`, so the two filters see identical inputs and
// identical upstream rejection. Nothing here touches the live path.
//
// Synthetic only: no cameras, calibration files, or model weights are used.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>

#include "dualview/hand_tracker.hpp"
using namespace dualview;
using Json = nlohmann::json;
namespace {

constexpr double kFocalPx = 550;
constexpr double kStepMs = 1000. / 30;  // 30 Hz pair rate.
constexpr int kSteps = 150;
constexpr int kWarmup = 15;

double percentile(std::vector<double> values, double q) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  return values[static_cast<std::size_t>(std::ceil(q * values.size())) - 1];
}
double rms(const std::vector<double>& values) {
  if (values.empty()) return 0;
  double total = 0;
  for (const auto value : values) total += value * value;
  return std::sqrt(total / values.size());
}

// Per-axis constant-velocity Kalman filter. With diagonal Q and R the 6-state
// position/velocity filter separates exactly into three 2-state filters, so
// this is the same estimator written in the form that makes that obvious.
class AxisKalman {
 public:
  void reset() { initialized_ = false; }
  double update(double measurement, double variance, double dt, double acceleration_sigma) {
    if (!initialized_) {
      position_ = measurement;
      velocity_ = 0;
      p00_ = variance;
      p01_ = 0;
      p11_ = acceleration_sigma * acceleration_sigma;
      initialized_ = true;
      return position_;
    }
    predict(dt, acceleration_sigma);
    const double innovation = measurement - position_;
    const double s = p00_ + variance;
    const double k0 = p00_ / s, k1 = p01_ / s;
    position_ += k0 * innovation;
    velocity_ += k1 * innovation;
    const double p00 = p00_, p01 = p01_;
    p00_ -= k0 * p00;
    p01_ -= k0 * p01;
    p11_ -= k1 * p01;
    return position_;
  }
  double predict_only(double dt, double acceleration_sigma) {
    if (!initialized_) return position_;
    predict(dt, acceleration_sigma);
    return position_;
  }

 private:
  void predict(double dt, double acceleration_sigma) {
    position_ += velocity_ * dt;
    const double q = acceleration_sigma * acceleration_sigma;
    const double dt2 = dt * dt, dt3 = dt2 * dt, dt4 = dt2 * dt2;
    p00_ += 2 * dt * p01_ + dt2 * p11_ + q * dt4 / 4;
    p01_ += dt * p11_ + q * dt3 / 2;
    p11_ += q * dt2;
  }
  double position_ = 0, velocity_ = 0;
  double p00_ = 1, p01_ = 0, p11_ = 1;
  bool initialized_ = false;
};

// Stereo triangulation error is anisotropic: lateral uncertainty scales as
// Z/f, depth uncertainty as Z^2/(f*B). Deriving R from the geometry is the one
// thing a scalar blend structurally cannot express.
struct KalmanHand {
  AxisKalman axes[21][3];
  void reset() {
    for (auto& landmark : axes)
      for (auto& axis : landmark) axis.reset();
  }
  Pose update(const Pose& measured, double dt, double noise_px, double baseline,
              double acceleration_sigma, bool isotropic = false) {
    Pose out(measured.size());
    for (std::size_t j = 0; j < measured.size(); ++j) {
      const double depth = std::max(.05, measured[j][2]);
      const double lateral_sigma = depth * noise_px / kFocalPx;
      const double depth_sigma =
          isotropic ? lateral_sigma : depth * depth * noise_px / (kFocalPx * baseline);
      const double variance[3] = {lateral_sigma * lateral_sigma, lateral_sigma * lateral_sigma,
                                  depth_sigma * depth_sigma};
      for (int axis = 0; axis < 3; ++axis)
        out[j][axis] =
            axes[j][axis].update(measured[j][axis], variance[axis], dt, acceleration_sigma);
    }
    return out;
  }
  Pose predict(std::size_t count, double dt, double acceleration_sigma) {
    Pose out(count);
    for (std::size_t j = 0; j < count; ++j)
      for (int axis = 0; axis < 3; ++axis)
        out[j][axis] = axes[j][axis].predict_only(dt, acceleration_sigma);
    return out;
  }
};

// The production filter blends position only: filtered = .35*prior + .65*measurement.
// A velocity term is computed alongside it but is used solely for dropout
// extrapolation, never fed back into the position estimate. This is that same
// filter with the feedforward connected -- a true alpha-beta predictor.
struct AlphaBetaHand {
  Pose position;
  std::vector<cv::Vec3d> velocity;
  bool initialized = false;
  Pose update(const Pose& measured, double dt, double alpha, double beta) {
    if (!initialized) {
      position = measured;
      velocity.assign(measured.size(), cv::Vec3d{});
      initialized = true;
      return position;
    }
    for (std::size_t j = 0; j < measured.size(); ++j) {
      const cv::Vec3d predicted = position[j] + velocity[j] * dt;
      const cv::Vec3d residual = measured[j] - predicted;
      position[j] = predicted + residual * alpha;
      velocity[j] += residual * (beta / dt);
    }
    return position;
  }
  Pose predict(double dt) {
    if (!initialized) return {};
    for (std::size_t j = 0; j < position.size(); ++j) position[j] += velocity[j] * dt;
    return position;
  }
};

struct Scenario {
  const char* name;
  double depth, baseline, noise_px;
  double lateral_amplitude_m, depth_amplitude_m, frequency_hz;
  double exposure_delay_ms = 0;  // Unobserved phone exposure lag, as a bias.
  int dropout_every = 0;         // Drop the phone detection every Nth step.
};

// Truth trajectory: the hand oscillates laterally and/or in depth about a centre.
cv::Vec3d centre_at(const Scenario& scenario, double seconds) {
  const double phase = 2 * std::numbers::pi * scenario.frequency_hz * seconds;
  return {scenario.lateral_amplitude_m * std::sin(phase), 0,
          scenario.depth + scenario.depth_amplitude_m * std::sin(phase)};
}

struct Arm {
  std::vector<double> error, lateral, depth;
  void add(const Pose& estimate, const Pose& truth) {
    for (std::size_t j = 0; j < truth.size(); ++j) {
      const auto delta = estimate[j] - truth[j];
      error.push_back(1000 * cv::norm(delta));
      lateral.push_back(1000 * std::hypot(delta[0], delta[1]));
      depth.push_back(1000 * std::abs(delta[2]));
    }
  }
  Json json() const {
    if (error.empty()) return nullptr;
    return {{"p50_mm", percentile(error, .5)},
            {"p95_mm", percentile(error, .95)},
            {"lateral_rms_mm", rms(lateral)},
            {"depth_rms_mm", rms(depth)},
            {"samples", error.size()}};
  }
};

Json evaluate(const Scenario& scenario, double acceleration_sigma, bool isotropic, double alpha,
              double beta) {
  std::mt19937 rng(7319);
  std::normal_distribution<double> noise(0, scenario.noise_px);
  CameraIntrinsics cameras[2];
  StereoSolution stereo;
  stereo.calibrated = true;
  stereo.translation_phone_from_laptop_m = {-scenario.baseline, 0, 0};
  CameraFrame frames[2];
  for (int i = 0; i < 2; ++i) {
    cameras[i].camera_id = i == 0 ? "laptop" : "phone";
    cameras[i].width = 640;
    cameras[i].height = 480;
    cameras[i].camera_matrix = {kFocalPx, 0, 320, 0, kFocalPx, 240, 0, 0, 1};
    cameras[i].distortion_coefficients = cv::Mat::zeros(1, 5, CV_64F);
    cameras[i].usable_for_metric_calibration = true;
    frames[i].image = cv::Mat::zeros(480, 640, CV_8UC3);
  }
  HandTracker tracker;  // Persistent across the sequence: filtering is the subject.
  KalmanHand kalman;
  AlphaBetaHand alphabeta;
  Arm raw_arm, ema_arm, kalman_arm, ab_fixed_arm;
  const auto base = Clock::now();
  int measured_steps = 0, predicted_steps = 0;
  for (int step = 0; step < kSteps; ++step) {
    const double seconds = step * kStepMs / 1000.;
    const auto now =
        base + std::chrono::microseconds(static_cast<long long>(step * kStepMs * 1000));
    for (auto& frame : frames) {
      frame.received = now;
      ++frame.sequence;
    }
    const auto centre = centre_at(scenario, seconds);
    // The phone observes the hand as it was `exposure_delay_ms` earlier, while
    // both frames carry the same receive time: a bias, not zero-mean noise.
    const auto phone_centre = centre_at(scenario, seconds - scenario.exposure_delay_ms / 1000.);
    Pose truth;
    std::vector<Hand> views[2];
    for (int i = 0; i < 2; ++i) {
      views[i].resize(1);
      views[i][0].confidence = .95f;
    }
    for (int j = 0; j < 21; ++j) {
      const cv::Vec3d offset((j % 5 - 2) * .012, (j / 5 - 2) * .015, .002 * (j % 3));
      const cv::Vec3d p = centre + offset;
      truth.push_back(p);
      for (int camera = 0; camera < 2; ++camera) {
        const cv::Vec3d seen = (camera == 0 ? centre : phone_centre) + offset;
        views[camera][0].landmarks[j] = {
            static_cast<float>(kFocalPx * (seen[0] - camera * scenario.baseline) / seen[2] + 320 +
                               noise(rng)),
            static_cast<float>(kFocalPx * seen[1] / seen[2] + 240 + noise(rng))};
      }
    }
    if (scenario.dropout_every && step % scenario.dropout_every == 0) views[1].clear();
    const auto result =
        tracker.track(frames, views[0], views[1], cameras, stereo, 0, 80, false, now);
    const auto& hand = result.hands[0];
    const double dt = kStepMs / 1000.;
    Pose kalman_pose, ab_fixed_pose;
    if (hand.source == PoseSource::Measured) {
      ++measured_steps;
      kalman_pose = kalman.update(hand.raw, dt, scenario.noise_px, scenario.baseline,
                                  acceleration_sigma, isotropic);
      ab_fixed_pose = alphabeta.update(hand.raw, dt, alpha, beta);
    } else if (hand.source == PoseSource::Predicted) {
      ++predicted_steps;
      kalman_pose = kalman.predict(21, dt, acceleration_sigma);
      ab_fixed_pose = alphabeta.predict(dt);
    }
    if (step < kWarmup || hand.filtered.empty()) continue;
    if (hand.source == PoseSource::Measured) raw_arm.add(hand.raw, truth);
    ema_arm.add(hand.filtered, truth);
    if (!kalman_pose.empty()) kalman_arm.add(kalman_pose, truth);
    if (!ab_fixed_pose.empty()) ab_fixed_arm.add(ab_fixed_pose, truth);
  }
  return {{"scenario", scenario.name},
          {"depth_m", scenario.depth},
          {"baseline_m", scenario.baseline},
          {"pixel_noise_sigma", scenario.noise_px},
          {"exposure_delay_ms", scenario.exposure_delay_ms},
          {"measured_steps", measured_steps},
          {"predicted_steps", predicted_steps},
          {"raw", raw_arm.json()},
          {"alpha_only_production", ema_arm.json()},
          {"alpha_beta_fixed", ab_fixed_arm.json()},
          {"kalman", kalman_arm.json()}};
}

}  // namespace

int main(int argc, char** argv) {
  double acceleration_sigma = 6;  // m/s^2 process noise; hand motion is jerky.
  bool isotropic = false;         // Ablation: ignore the depth/lateral uncertainty split.
  double alpha = .65, beta = .2;  // Production alpha, plus the missing beta term.
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("--accel=", 0) == 0) acceleration_sigma = std::stod(arg.substr(8));
    if (arg == "--isotropic") isotropic = true;
    if (arg.rfind("--beta=", 0) == 0) beta = std::stod(arg.substr(7));
  }
  const Scenario scenarios[] = {
      {"static_near", .35, .12, 1, 0, 0, 0},
      {"static_mid", .60, .12, 1, 0, 0, 0},
      {"static_far", 1.00, .12, 1, 0, 0, 0},
      {"static_short_baseline", 1.00, .06, 1, 0, 0, 0},
      {"lateral_slow", .60, .12, 1, .10, 0, .5},
      {"lateral_fast", .60, .12, 1, .10, 0, 1.5},
      {"depth_slow", .60, .12, 1, 0, .10, .5},
      {"depth_fast", .60, .12, 1, 0, .10, 1.5},
      {"dropout_every_4", .60, .12, 1, .10, 0, .5, 0, 4},
      {"hidden_delay_40ms", .60, .12, 1, .10, 0, .5, 40},
  };
  Json report = {{"kind", "filter_ab_synthetic_not_physical_accuracy"},
                 {"acceleration_sigma_m_s2", acceleration_sigma},
                 {"isotropic_measurement_noise", isotropic},
                 {"alpha", alpha},
                 {"beta", beta},
                 {"pair_rate_hz", 1000. / kStepMs},
                 {"steps", kSteps},
                 {"warmup_steps", kWarmup},
                 {"note",
                  "Both filters consume identical measurements. alpha_beta is the production "
                  "filter; kalman is fed the same raw triangulation."}};
  report["scenarios"] = Json::array();
  for (const auto& scenario : scenarios)
    report["scenarios"].push_back(evaluate(scenario, acceleration_sigma, isotropic, alpha, beta));
  std::cout << report.dump(2) << '\n';
  return 0;
}
