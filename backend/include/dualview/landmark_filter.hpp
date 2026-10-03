#pragma once
#include <array>
#include <cmath>
#include <opencv2/core.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace dualview {
using Pose = std::vector<cv::Vec3d>;
inline constexpr int kLandmarkCount = 21;

// Which estimator produces the published pose.
//   AlphaOnly  - the original position-only blend. Default; the supported path.
//   AlphaBeta  - the same blend with its velocity feedforward connected.
//   Kalman     - experimental constant-velocity filter with anisotropic noise.
enum class FilterStrategy { AlphaOnly, AlphaBeta, Kalman };

inline const char* filter_strategy_name(FilterStrategy strategy) {
  switch (strategy) {
    case FilterStrategy::AlphaBeta:
      return "alpha_beta";
    case FilterStrategy::Kalman:
      return "kalman";
    default:
      return "alpha";
  }
}
inline FilterStrategy parse_filter_strategy(const std::string& name) {
  if (name == "alpha") return FilterStrategy::AlphaOnly;
  if (name == "alpha_beta") return FilterStrategy::AlphaBeta;
  if (name == "kalman") return FilterStrategy::Kalman;
  throw std::invalid_argument("tracking filter must be alpha, alpha_beta, or kalman");
}

// Tuning shared by the non-default estimators. Defaults come from the synthetic
// sweep in backend/benchmarks/filter_ab.cpp; they are not physically validated.
struct FilterTuning {
  double alpha = .65;             // Weight on a new measurement.
  double beta = .35;              // Weight on the velocity correction.
  double kalman_accel_sigma = 4;  // Process noise, m/s^2.
  double kalman_noise_px = 1.5;   // Assumed landmark noise, pixels.
};

// filtered = predicted + alpha * (measurement - predicted), with the velocity
// estimate advanced by the same residual. With beta = 0 this reduces to the
// AlphaOnly blend.
struct AlphaBetaFilter {
  Pose position;
  Pose velocity;
  bool initialized = false;

  void reset() {
    position.clear();
    velocity.clear();
    initialized = false;
  }
  Pose update(const Pose& measured, double dt, const FilterTuning& tuning) {
    if (!initialized || position.size() != measured.size() || dt <= 0) {
      position = measured;
      velocity.assign(measured.size(), cv::Vec3d{});
      initialized = true;
      return position;
    }
    for (std::size_t j = 0; j < measured.size(); ++j) {
      const cv::Vec3d predicted = position[j] + velocity[j] * dt;
      const cv::Vec3d residual = measured[j] - predicted;
      position[j] = predicted + residual * tuning.alpha;
      velocity[j] += residual * (tuning.beta / dt);
    }
    return position;
  }
  Pose predict(double dt) {
    if (!initialized) return {};
    for (std::size_t j = 0; j < position.size(); ++j) position[j] += velocity[j] * dt;
    return position;
  }
};

// Per-axis constant-velocity Kalman filter. With diagonal Q and R the 6-state
// position/velocity form separates exactly into three 2-state filters, so this
// is the same estimator written to make that separation explicit.
//
// Measurement noise is anisotropic by construction: stereo lateral uncertainty
// scales as Z/f, depth uncertainty as Z^2/(f*B). A scalar blend cannot express
// that difference; whether it helps depends on how much real motion is in depth.
struct KalmanFilter {
  struct Axis {
    double position = 0, velocity = 0;
    double p00 = 1, p01 = 0, p11 = 1;
    bool initialized = false;

    void advance(double dt, double accel_sigma) {
      position += velocity * dt;
      const double q = accel_sigma * accel_sigma;
      const double dt2 = dt * dt, dt3 = dt2 * dt, dt4 = dt2 * dt2;
      p00 += 2 * dt * p01 + dt2 * p11 + q * dt4 / 4;
      p01 += dt * p11 + q * dt3 / 2;
      p11 += q * dt2;
    }
    double update(double measurement, double variance, double dt, double accel_sigma) {
      if (!initialized) {
        position = measurement;
        velocity = 0;
        p00 = variance;
        p01 = 0;
        p11 = accel_sigma * accel_sigma;
        initialized = true;
        return position;
      }
      advance(dt, accel_sigma);
      const double innovation = measurement - position;
      const double s = p00 + variance;
      if (!(s > 0)) return position;
      const double k0 = p00 / s, k1 = p01 / s;
      position += k0 * innovation;
      velocity += k1 * innovation;
      const double prior00 = p00, prior01 = p01;
      p00 -= k0 * prior00;
      p01 -= k0 * prior01;
      p11 -= k1 * prior01;
      return position;
    }
  };
  std::array<std::array<Axis, 3>, kLandmarkCount> axes{};
  bool initialized = false;

  void reset() {
    axes = {};
    initialized = false;
  }
  Pose update(const Pose& measured, double dt, double baseline_m, double focal_px,
              const FilterTuning& tuning) {
    if (measured.size() != kLandmarkCount || !(dt > 0) || !(baseline_m > 0) || !(focal_px > 0))
      return measured;
    Pose out(measured.size());
    for (std::size_t j = 0; j < measured.size(); ++j) {
      const double depth = std::max(.05, measured[j][2]);
      const double lateral = depth * tuning.kalman_noise_px / focal_px;
      const double range = depth * depth * tuning.kalman_noise_px / (focal_px * baseline_m);
      const double variance[3] = {lateral * lateral, lateral * lateral, range * range};
      for (int axis = 0; axis < 3; ++axis)
        out[j][axis] =
            axes[j][axis].update(measured[j][axis], variance[axis], dt, tuning.kalman_accel_sigma);
    }
    initialized = true;
    return out;
  }
  Pose predict(double dt, const FilterTuning& tuning) {
    if (!initialized) return {};
    Pose out(kLandmarkCount);
    for (std::size_t j = 0; j < kLandmarkCount; ++j)
      for (int axis = 0; axis < 3; ++axis) {
        axes[j][axis].advance(dt, tuning.kalman_accel_sigma);
        out[j][axis] = axes[j][axis].position;
      }
    return out;
  }
};

// Root-mean-square distance between two poses, in millimetres.
inline double pose_rms_mm(const Pose& a, const Pose& b) {
  if (a.size() != b.size() || a.empty()) return 0;
  double total = 0;
  for (std::size_t j = 0; j < a.size(); ++j) {
    const double distance = cv::norm(a[j] - b[j]);
    total += distance * distance;
  }
  return 1000 * std::sqrt(total / a.size());
}
}  // namespace dualview
