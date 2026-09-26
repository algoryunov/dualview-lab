#include <cassert>
#include <chrono>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <string>
#include <vector>

#include "dualview/calibration.hpp"

namespace {

dualview::CameraIntrinsics test_intrinsics() {
  return {"camera-a",
          "test",
          640,
          480,
          {500.0, 0.0, 320.0, 0.0, 500.0, 240.0, 0.0, 0.0, 1.0},
          cv::Mat::zeros(1, 5, CV_64F),
          true};
}

void test_profile_scaling() {
  const auto intrinsics = test_intrinsics();
  std::string error;
  const auto scaled = intrinsics.scaled_for_active_frame("camera-a", 1280, 960, &error);
  assert(scaled.has_value());
  assert(scaled->camera_matrix(0, 0) == 1000.0);
  assert(scaled->camera_matrix(0, 2) == 640.0);
  assert(!intrinsics.scaled_for_active_frame("camera-a", 1280, 720, &error));
}

void test_relative_transform() {
  const dualview::TargetPose laptop{cv::Matx33d::eye(), {0.0, 0.0, 1.0}, 0.2, 12};
  cv::Mat rotation_matrix;
  cv::Rodrigues(cv::Vec3d{0.0, 0.0, CV_PI / 2.0}, rotation_matrix);
  const dualview::TargetPose phone{cv::Matx33d(rotation_matrix), {0.2, 0.0, 1.0}, 0.2, 12};
  const auto [rotation, translation] = dualview::relative_transform(laptop, phone);
  assert(cv::norm(rotation - phone.rotation) < 1e-10);
  assert(cv::norm(translation - cv::Vec3d{0.2, 0.0, 0.0}) < 1e-10);
}

void test_reprojection_error() {
  const std::vector<cv::Point3d> points{{0.0, 0.0, 1.0}, {0.1, -0.1, 2.0}, {-0.2, 0.1, 1.5}};
  const cv::Matx33d matrix{800.0, 0.0, 320.0, 0.0, 800.0, 240.0, 0.0, 0.0, 1.0};
  std::vector<cv::Point2d> projected;
  cv::projectPoints(points, cv::Vec3d{}, cv::Vec3d{}, cv::Mat(matrix), cv::noArray(), projected);
  assert(dualview::reprojection_error(points, projected, matrix, cv::Matx33d::eye(), {}) < 1e-9);
}

void test_stereo_solution() {
  std::vector<dualview::CalibrationObservation> observations;
  for (int index = 0; index < 5; ++index) {
    observations.push_back({cv::Matx33d::eye(), {0.1, 0.0, 0.0}, 0.6, 24.0});
  }
  const auto solution = dualview::solve_stereo_observations(observations);
  assert(solution.calibrated);
  assert(solution.health == dualview::CalibrationHealth::kHealthy);
  assert(std::abs(solution.baseline_cm - 10.0) < 1e-12);
  assert(std::abs(solution.median_reprojection_error_px - 0.6) < 1e-12);
}

void test_generated_charuco_board_is_detected() {
  const dualview::CharucoSpec spec;
  const auto board = dualview::make_charuco_board(spec);
  cv::Mat grayscale;
  board.generateImage({spec.squares_x * 200, spec.squares_y * 200}, grayscale);
  cv::Mat bgr;
  cv::cvtColor(grayscale, bgr, cv::COLOR_GRAY2BGR);
  const auto observation = dualview::detect_intrinsic_observation(bgr, spec);
  assert(observation.has_value());
  assert(observation->ids.rows >= 12);
  assert(observation->coverage > 0.025);
}

}  // namespace

int main() {
  const dualview::StereoSolution uncalibrated;
  assert(!uncalibrated.calibrated);
  assert(cv::norm(uncalibrated.rotation_phone_from_laptop - cv::Matx33d::eye()) == 0);
  assert(cv::norm(uncalibrated.translation_phone_from_laptop_m) == 0);
  test_profile_scaling();
  test_relative_transform();
  test_reprojection_error();
  test_stereo_solution();
  test_generated_charuco_board_is_detected();
}
