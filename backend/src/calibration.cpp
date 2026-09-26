#include "dualview/calibration.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <opencv2/imgproc.hpp>
#include <utility>

namespace dualview {
namespace {

constexpr int kMinimumPoseCorners = 8;
constexpr int kMinimumIntrinsicCorners = 12;
constexpr int kMinimumIntrinsicViews = 15;
constexpr int kMinimumStereoViews = 5;

double median(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
  std::nth_element(values.begin(), middle, values.end());
  if (values.size() % 2 != 0) {
    return *middle;
  }
  const auto upper = *middle;
  std::nth_element(values.begin(), middle - 1, values.end());
  return (upper + *(middle - 1)) / 2.0;
}

bool sharp_enough(const cv::Mat& gray, const double threshold) {
  cv::Mat laplacian;
  cv::Laplacian(gray, laplacian, CV_64F);
  cv::Scalar mean;
  cv::Scalar standard_deviation;
  cv::meanStdDev(laplacian, mean, standard_deviation);
  return standard_deviation[0] * standard_deviation[0] >= threshold;
}

std::optional<std::pair<cv::Mat, cv::Mat>> detect_charuco_corners(
    const cv::Mat& gray, const cv::aruco::CharucoBoard& board,
    const cv::Mat& camera_matrix = cv::Mat(), const cv::Mat& distortion = cv::Mat()) {
  cv::aruco::CharucoParameters parameters;
  parameters.cameraMatrix = camera_matrix;
  parameters.distCoeffs = distortion;
  const cv::aruco::CharucoDetector detector(board, parameters);
  cv::Mat charuco_corners;
  cv::Mat charuco_ids;
  detector.detectBoard(gray, charuco_corners, charuco_ids);
  if (charuco_ids.empty()) {
    return std::nullopt;
  }
  return std::pair{charuco_corners, charuco_ids};
}

cv::Mat matrix_from(const cv::Matx33d& matrix) {
  return cv::Mat(3, 3, CV_64F, const_cast<double*>(matrix.val)).clone();
}

cv::Matx33d matx_from(const cv::Mat& matrix) {
  cv::Mat converted;
  matrix.convertTo(converted, CV_64F);
  cv::Matx33d result;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      result(row, column) = converted.at<double>(row, column);
    }
  }
  return result;
}

cv::Vec3d vec3_from(const cv::Mat& vector) {
  cv::Mat converted;
  vector.reshape(1, 3).convertTo(converted, CV_64F);
  return {converted.at<double>(0), converted.at<double>(1), converted.at<double>(2)};
}

}  // namespace

bool CameraIntrinsics::valid() const {
  if (camera_id.empty() || width <= 0 || height <= 0 || !usable_for_metric_calibration ||
      distortion_coefficients.total() < 4) {
    return false;
  }
  for (const auto value : camera_matrix.val) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  cv::Mat flattened = distortion_coefficients.reshape(1, 1);
  for (int index = 0; index < flattened.cols; ++index) {
    const auto value =
        flattened.depth() == CV_64F ? flattened.at<double>(index) : flattened.at<float>(index);
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

std::optional<CameraIntrinsics> CameraIntrinsics::scaled_for_active_frame(
    const std::string& expected_camera_id, const int active_width, const int active_height,
    std::string* error) const {
  const auto fail = [&](const std::string& message) -> std::optional<CameraIntrinsics> {
    if (error != nullptr) {
      *error = message;
    }
    return std::nullopt;
  };
  if (!valid()) {
    return fail("Intrinsic profile is missing required valid values.");
  }
  if (camera_id != expected_camera_id) {
    return fail("Intrinsic profile camera ID does not match the active camera.");
  }
  if (active_width <= 0 || active_height <= 0) {
    return fail("Active stream has no valid resolution.");
  }
  const auto profile_aspect = static_cast<double>(width) / height;
  const auto active_aspect = static_cast<double>(active_width) / active_height;
  if (std::abs(profile_aspect - active_aspect) > 0.001) {
    return fail("Active stream aspect ratio differs from the intrinsic profile.");
  }
  const auto x_scale = static_cast<double>(active_width) / width;
  const auto y_scale = static_cast<double>(active_height) / height;
  if (std::abs(x_scale - y_scale) > 0.001) {
    return fail("Active stream is not a uniform resize of the intrinsic profile.");
  }
  auto scaled = *this;
  scaled.width = active_width;
  scaled.height = active_height;
  scaled.camera_matrix(0, 0) *= x_scale;
  scaled.camera_matrix(0, 1) *= x_scale;
  scaled.camera_matrix(0, 2) *= x_scale;
  scaled.camera_matrix(1, 0) *= y_scale;
  scaled.camera_matrix(1, 1) *= y_scale;
  scaled.camera_matrix(1, 2) *= y_scale;
  scaled.camera_matrix(2, 2) = 1.0;
  return scaled;
}

cv::aruco::CharucoBoard make_charuco_board(const CharucoSpec& spec) {
  const auto dictionary = cv::aruco::getPredefinedDictionary(spec.dictionary_id);
  return {cv::Size{spec.squares_x, spec.squares_y}, static_cast<float>(spec.square_length_m),
          static_cast<float>(spec.square_length_m * spec.marker_to_square_ratio), dictionary};
}

std::optional<IntrinsicObservation> detect_intrinsic_observation(const cv::Mat& bgr,
                                                                 const CharucoSpec& spec) {
  if (bgr.empty()) {
    return std::nullopt;
  }
  cv::Mat gray;
  cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
  if (!sharp_enough(gray, 55.0)) {
    return std::nullopt;
  }
  const auto detected = detect_charuco_corners(gray, make_charuco_board(spec));
  if (!detected || detected->second.rows < kMinimumIntrinsicCorners) {
    return std::nullopt;
  }
  std::vector<cv::Point2f> points;
  detected->first.copyTo(points);
  std::vector<cv::Point2f> hull;
  cv::convexHull(points, hull);
  const auto hull_area = cv::contourArea(hull);
  const auto coverage = hull_area / static_cast<double>(bgr.rows * bgr.cols);
  if (coverage < 0.025) {
    return std::nullopt;
  }
  cv::Point2d center;
  for (const auto& point : points) {
    center.x += point.x;
    center.y += point.y;
  }
  center *= 1.0 / points.size();
  return IntrinsicObservation{detected->first, detected->second, coverage, center};
}

std::optional<IntrinsicSolution> solve_intrinsics(
    const std::vector<IntrinsicObservation>& observations, const cv::Size image_size,
    const CharucoSpec& spec, std::string* error) {
  if (observations.size() < kMinimumIntrinsicViews) {
    if (error != nullptr) {
      *error = "Need at least 15 accepted ChArUco views for intrinsic calibration.";
    }
    return std::nullopt;
  }
  std::vector<cv::Mat> corners;
  std::vector<cv::Mat> ids;
  corners.reserve(observations.size());
  ids.reserve(observations.size());
  for (const auto& observation : observations) {
    corners.push_back(observation.corners);
    ids.push_back(observation.ids);
  }
  cv::Mat camera_matrix;
  cv::Mat distortion;
  const auto board = cv::makePtr<cv::aruco::CharucoBoard>(make_charuco_board(spec));
  const auto rms =
      cv::aruco::calibrateCameraCharuco(corners, ids, board, image_size, camera_matrix, distortion);
  if (!std::isfinite(rms) || rms > 2.5 || !cv::checkRange(camera_matrix) ||
      !cv::checkRange(distortion)) {
    if (error)
      *error =
          "Intrinsic reprojection error is too high; capture sharper, more varied target views.";
    return std::nullopt;
  }
  return IntrinsicSolution{rms, matx_from(camera_matrix), distortion.reshape(1, 1)};
}

std::optional<TargetPose> detect_target_pose(const cv::Mat& bgr, const CameraIntrinsics& intrinsics,
                                             const CharucoSpec& spec) {
  if (bgr.empty() || !intrinsics.valid()) {
    return std::nullopt;
  }
  cv::Mat gray;
  cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
  if (!sharp_enough(gray, 45.0)) {
    return std::nullopt;
  }
  const auto board = make_charuco_board(spec);
  const auto detected = detect_charuco_corners(gray, board, matrix_from(intrinsics.camera_matrix),
                                               intrinsics.distortion_coefficients);
  if (!detected || detected->second.rows < kMinimumPoseCorners) {
    return std::nullopt;
  }
  std::vector<cv::Point3f> object_points;
  const auto board_points = board.getChessboardCorners();
  object_points.reserve(static_cast<std::size_t>(detected->second.rows));
  for (int index = 0; index < detected->second.rows; ++index) {
    object_points.push_back(board_points[detected->second.at<int>(index)]);
  }
  std::vector<cv::Point2f> image_points;
  detected->first.copyTo(image_points);
  cv::Mat rvec;
  cv::Mat tvec;
  const auto solved =
      cv::solvePnP(object_points, image_points, matrix_from(intrinsics.camera_matrix),
                   intrinsics.distortion_coefficients, rvec, tvec);
  if (!solved) {
    return std::nullopt;
  }
  std::vector<cv::Point2f> projected;
  cv::projectPoints(object_points, rvec, tvec, matrix_from(intrinsics.camera_matrix),
                    intrinsics.distortion_coefficients, projected);
  double total_error = 0.0;
  for (std::size_t index = 0; index < image_points.size(); ++index) {
    total_error += cv::norm(projected[index] - image_points[index]);
  }
  cv::Mat rotation;
  cv::Rodrigues(rvec, rotation);
  return TargetPose{matx_from(rotation), vec3_from(tvec), total_error / image_points.size(),
                    static_cast<int>(image_points.size())};
}

std::pair<cv::Matx33d, cv::Vec3d> relative_transform(const TargetPose& laptop_pose,
                                                     const TargetPose& phone_pose) {
  const auto rotation = phone_pose.rotation * laptop_pose.rotation.t();
  return {rotation, phone_pose.translation_m - rotation * laptop_pose.translation_m};
}

double reprojection_error(const std::vector<cv::Point3d>& points_3d,
                          const std::vector<cv::Point2d>& points_2d,
                          const cv::Matx33d& camera_matrix, const cv::Matx33d& rotation,
                          const cv::Vec3d& translation_m) {
  if (points_3d.empty() || points_3d.size() != points_2d.size()) {
    return std::numeric_limits<double>::infinity();
  }
  cv::Mat rvec;
  cv::Rodrigues(matrix_from(rotation), rvec);
  std::vector<cv::Point2d> projected;
  cv::projectPoints(points_3d, rvec, translation_m, matrix_from(camera_matrix), cv::noArray(),
                    projected);
  double total_error = 0.0;
  for (std::size_t index = 0; index < points_2d.size(); ++index) {
    total_error += cv::norm(projected[index] - points_2d[index]);
  }
  return total_error / points_2d.size();
}

StereoSolution solve_stereo_observations(const std::vector<CalibrationObservation>& observations) {
  if (observations.size() < kMinimumStereoViews) {
    return {false,
            CalibrationHealth::kInvalid,
            "Need at least five accepted paired ChArUco views.",
            static_cast<int>(observations.size()),
            0.0,
            0.0,
            0.0,
            cv::Matx33d::eye(),
            {}};
  }
  cv::Mat summed_rotation = cv::Mat::zeros(3, 3, CV_64F);
  std::vector<double> translations_x;
  std::vector<double> translations_y;
  std::vector<double> translations_z;
  std::vector<double> reprojection_errors;
  std::vector<double> pairing_errors;
  for (const auto& observation : observations) {
    summed_rotation += matrix_from(observation.rotation_phone_from_laptop);
    translations_x.push_back(observation.translation_phone_from_laptop_m[0]);
    translations_y.push_back(observation.translation_phone_from_laptop_m[1]);
    translations_z.push_back(observation.translation_phone_from_laptop_m[2]);
    reprojection_errors.push_back(observation.reprojection_error_px);
    pairing_errors.push_back(observation.pairing_error_ms);
  }
  cv::SVD svd(summed_rotation);
  cv::Mat rotation = svd.u * svd.vt;
  if (cv::determinant(rotation) < 0.0) {
    cv::Mat corrected_u = svd.u.clone();
    corrected_u.col(2) *= -1.0;
    rotation = corrected_u * svd.vt;
  }
  const cv::Vec3d translation{median(translations_x), median(translations_y),
                              median(translations_z)};
  const auto median_error = median(reprojection_errors);
  const auto health =
      median_error <= 1.5 ? CalibrationHealth::kHealthy : CalibrationHealth::kDegraded;
  return {true,
          health,
          health == CalibrationHealth::kHealthy
              ? "Calibration completed."
              : "Calibration completed with elevated reprojection error.",
          static_cast<int>(observations.size()),
          cv::norm(translation) * 100.0,
          median_error,
          median(pairing_errors),
          matx_from(rotation),
          translation};
}

}  // namespace dualview
