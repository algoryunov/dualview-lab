#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

#include "dualview/hand_tracker.hpp"
using namespace dualview;
using Json = nlohmann::json;
namespace {
double percentile(std::vector<double> values, double q) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  return values[static_cast<std::size_t>(std::ceil(q * values.size())) - 1];
}
struct Scenario {
  const char* name;
  double baseline, depth, noise_px, motion_delay_ms;
  double wrong_baseline_fraction = 0, vertical_outlier_px = 0;
};
Json evaluate(Scenario scenario) {
  constexpr int trials = 200;
  std::mt19937 rng(7319);
  std::normal_distribution<double> noise(0, scenario.noise_px);
  CameraIntrinsics cameras[2];
  StereoSolution stereo;
  stereo.calibrated = true;
  stereo.translation_phone_from_laptop_m = {
      -scenario.baseline * (1 + scenario.wrong_baseline_fraction), 0, 0};
  CameraFrame frames[2];
  for (int i = 0; i < 2; ++i) {
    cameras[i].camera_id = i == 0 ? "laptop" : "phone";
    cameras[i].width = 640;
    cameras[i].height = 480;
    cameras[i].camera_matrix = {550, 0, 320, 0, 550, 240, 0, 0, 1};
    cameras[i].distortion_coefficients = cv::Mat::zeros(1, 5, CV_64F);
    cameras[i].usable_for_metric_calibration = true;
    frames[i].image = cv::Mat::zeros(480, 640, CV_8UC3);
  }
  std::vector<double> errors, residuals, durations;
  int accepted = 0;
  Json reasons = Json::object();
  for (int trial = 0; trial < trials + 10; ++trial) {
    const auto now = Clock::now();
    for (auto& frame : frames) frame.received = now;
    Pose truth;
    std::vector<Hand> views[2];
    for (int i = 0; i < 2; ++i) {
      views[i].resize(1);
      views[i][0].confidence = .95;
    }
    for (int j = 0; j < 21; ++j) {
      const cv::Vec3d p((j % 5 - 2) * .012, (j / 5 - 2) * .015, scenario.depth + .002 * (j % 3));
      truth.push_back(p);
      for (int camera = 0; camera < 2; ++camera) {
        const double shift = camera * .3 * scenario.motion_delay_ms / 1000.;
        views[camera][0].landmarks[j] = {
            static_cast<float>(550 * (p[0] + shift - camera * scenario.baseline) / p[2] + 320 +
                               noise(rng)),
            static_cast<float>(550 * p[1] / p[2] + 240 + noise(rng) +
                               (camera && j == 8 ? scenario.vertical_outlier_px : 0))};
      }
    }
    HandTracker tracker;  // Raw triangulation only; no previous measurements or prediction.
    const auto started = Clock::now();
    const auto result =
        tracker.track(frames, views[0], views[1], cameras, stereo, 0, 80, false, now);
    const double elapsed =
        std::chrono::duration<double, std::micro>(Clock::now() - started).count();
    if (trial < 10) continue;  // Timing warmup excluded from all reported aggregates.
    durations.push_back(elapsed);
    const auto reason = result.reason.empty() ? "measured" : result.reason;
    reasons[reason] = reasons.value(reason, 0) + 1;
    if (result.hands[0].source != PoseSource::Measured) continue;
    ++accepted;
    for (int j = 0; j < 21; ++j)
      errors.push_back(1000 * cv::norm(result.hands[0].raw[j] - truth[j]));
    for (auto error : result.hands[0].residuals) residuals.push_back(error);
  }
  const auto metric = [&](const std::vector<double>& data, double q) -> Json {
    return data.empty() ? Json(nullptr) : Json(percentile(data, q));
  };
  return {{"scenario", scenario.name},
          {"trials", trials},
          {"accepted", accepted},
          {"reasons", reasons},
          {"baseline_m", scenario.baseline},
          {"depth_m", scenario.depth},
          {"pixel_noise_sigma", scenario.noise_px},
          {"unobserved_exposure_delay_ms", scenario.motion_delay_ms},
          {"motion_speed_m_s", .3},
          {"calibration_baseline_error_fraction", scenario.wrong_baseline_fraction},
          {"landmark_error_mm_p50", metric(errors, .5)},
          {"landmark_error_mm_p95", metric(errors, .95)},
          {"reprojection_px_p50", metric(residuals, .5)},
          {"tracker_us_p50", metric(durations, .5)},
          {"tracker_us_p95", metric(durations, .95)},
          {"tracker_us_max", metric(durations, 1)}};
}
}  // namespace
int main(int argc, char** argv) {
  const bool check = argc == 2 && std::string(argv[1]) == "--check";
  Json rows = Json::array();
  for (const auto scenario :
       {Scenario{"ideal", .12, .6, 0, 0}, Scenario{"noise_near", .12, .35, 1, 0},
        Scenario{"noise_middle", .12, .6, 1, 0}, Scenario{"noise_far", .12, 1, 1, 0},
        Scenario{"short_baseline", .06, 1, 1, 0}, Scenario{"hidden_delay_40ms", .12, .6, 0, 40},
        Scenario{"hidden_delay_80ms", .12, .6, 0, 80},
        Scenario{"wrong_baseline_10pct", .12, .6, 0, 0, .1},
        Scenario{"vertical_outlier", .12, .6, 0, 0, 0, 40}})
    rows.push_back(evaluate(scenario));
  if (check) {
    if (rows[0]["accepted"] != 200 || rows[0]["landmark_error_mm_p95"].get<double>() > .01 ||
        rows[5]["landmark_error_mm_p50"].get<double>() < 50 ||
        rows[5]["reprojection_px_p50"].get<double>() > .01 || rows[8]["accepted"] != 0)
      return 1;
  }
  Json report = {
      {"schema_version", 1},
      {"kind", "synthetic_geometry_not_physical_device_accuracy"},
      {"seed", 7319},
      {"opencv", CV_VERSION},
      {"compiler", __VERSION__},
      {"build", DUALVIEW_BUILD_TYPE},
      {"image_size", {640, 480}},
      {"focal_length_px", 550},
      {"error_reference", "laptop exposure; individual raw landmarks of accepted measured poses"},
      {"timing_scope",
       "HandTracker only; excludes capture, inference, transport, rendering; steady_clock"},
      {"percentile_method", "nearest rank"},
      {"scenarios", rows}};
  std::cout << report.dump(2) << '\n';
}
