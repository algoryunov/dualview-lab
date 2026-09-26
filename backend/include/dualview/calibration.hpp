#pragma once

#include <opencv2/aruco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/objdetect/charuco_detector.hpp>
#include <optional>
#include <string>
#include <vector>

namespace dualview {

struct CharucoSpec {
  int squares_x{5};
  int squares_y{7};
  double square_length_m{0.035};
  double marker_to_square_ratio{0.026 / 0.035};
  int dictionary_id{cv::aruco::DICT_4X4_50};
};

struct CameraIntrinsics {
  std::string camera_id;
  std::string camera_mode;
  int width{};
  int height{};
  cv::Matx33d camera_matrix{};
  cv::Mat distortion_coefficients;
  bool usable_for_metric_calibration{false};

  [[nodiscard]] bool valid() const;
  [[nodiscard]] std::optional<CameraIntrinsics> scaled_for_active_frame(
      const std::string& expected_camera_id, int active_width, int active_height,
      std::string* error = nullptr) const;
};

struct TargetPose {
  cv::Matx33d rotation;
  cv::Vec3d translation_m;
  double reprojection_error_px{};
  int corner_count{};
};

struct IntrinsicObservation {
  cv::Mat corners;
  cv::Mat ids;
  double coverage{};
  cv::Point2d center_px;
};

struct IntrinsicSolution {
  double rms_reprojection_error_px{};
  cv::Matx33d camera_matrix;
  cv::Mat distortion_coefficients;
};

struct CalibrationObservation {
  cv::Matx33d rotation_phone_from_laptop{cv::Matx33d::eye()};
  cv::Vec3d translation_phone_from_laptop_m{};
  double reprojection_error_px{};
  double pairing_error_ms{};
};

enum class CalibrationHealth { kInvalid, kHealthy, kDegraded };

struct StereoSolution {
  bool calibrated{false};
  CalibrationHealth health{CalibrationHealth::kInvalid};
  std::string message;
  int accepted_views{};
  double baseline_cm{};
  double median_reprojection_error_px{};
  double median_pairing_error_ms{};
  cv::Matx33d rotation_phone_from_laptop{cv::Matx33d::eye()};
  cv::Vec3d translation_phone_from_laptop_m{};
};

[[nodiscard]] cv::aruco::CharucoBoard make_charuco_board(const CharucoSpec& spec);
[[nodiscard]] std::optional<IntrinsicObservation> detect_intrinsic_observation(
    const cv::Mat& bgr, const CharucoSpec& spec);
[[nodiscard]] std::optional<IntrinsicSolution> solve_intrinsics(
    const std::vector<IntrinsicObservation>& observations, cv::Size image_size,
    const CharucoSpec& spec, std::string* error = nullptr);
[[nodiscard]] std::optional<TargetPose> detect_target_pose(const cv::Mat& bgr,
                                                           const CameraIntrinsics& intrinsics,
                                                           const CharucoSpec& spec);
[[nodiscard]] std::pair<cv::Matx33d, cv::Vec3d> relative_transform(const TargetPose& laptop_pose,
                                                                   const TargetPose& phone_pose);
[[nodiscard]] double reprojection_error(const std::vector<cv::Point3d>& points_3d,
                                        const std::vector<cv::Point2d>& points_2d,
                                        const cv::Matx33d& camera_matrix,
                                        const cv::Matx33d& rotation,
                                        const cv::Vec3d& translation_m);
[[nodiscard]] StereoSolution solve_stereo_observations(
    const std::vector<CalibrationObservation>& observations);

}  // namespace dualview
