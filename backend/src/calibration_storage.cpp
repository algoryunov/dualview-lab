#include <filesystem>

#include "dualview/calibration_session.hpp"
namespace dualview {
void save_calibration(const std::string& path, const CameraIntrinsics (&intrinsics_)[2],
                      const StereoSolution& stereo_) {
  const std::filesystem::path destination(path);
  if (destination.has_parent_path()) std::filesystem::create_directories(destination.parent_path());
  const auto temporary = destination.string() + ".tmp.yml";
  cv::FileStorage file(temporary, cv::FileStorage::WRITE);
  if (!file.isOpened()) throw std::runtime_error("Cannot save calibration");
  for (int i = 0; i < 2; ++i) {
    const auto k = std::to_string(i);
    const auto& p = intrinsics_[i];
    file << "width" + k << p.width << "height" + k << p.height << "K" + k
         << cv::Mat(p.camera_matrix) << "D" + k << p.distortion_coefficients;
  }
  file << "health" << (stereo_.health == CalibrationHealth::kHealthy ? "healthy" : "degraded")
       << "calibrated" << static_cast<int>(stereo_.calibrated) << "R"
       << cv::Mat(stereo_.rotation_phone_from_laptop) << "T"
       << cv::Mat(stereo_.translation_phone_from_laptop_m) << "baseline_cm" << stereo_.baseline_cm
       << "error_px" << stereo_.median_reprojection_error_px;
  file.release();
  std::filesystem::rename(temporary, destination);
}
CalibrationGeometry read_calibration(const std::string& path) {
  CalibrationGeometry geometry;
  cv::FileStorage file(path, cv::FileStorage::READ);
  if (!file.isOpened()) throw std::runtime_error("Cannot load calibration");
  for (int i = 0; i < 2; ++i) {
    const auto k = std::to_string(i);
    auto& p = geometry.intrinsics[i];
    p.camera_id = i == 0 ? "laptop" : "phone";
    p.camera_mode = "default";
    file["width" + k] >> p.width;
    file["height" + k] >> p.height;
    cv::Mat m;
    file["K" + k] >> m;
    if (m.rows == 3 && m.cols == 3) p.camera_matrix = m;
    file["D" + k] >> p.distortion_coefficients;
    p.usable_for_metric_calibration = p.width > 0;
  }
  int calibrated = 0;
  file["calibrated"] >> calibrated;
  geometry.stereo.calibrated =
      calibrated && geometry.intrinsics[0].valid() && geometry.intrinsics[1].valid();
  cv::Mat r, t;
  file["R"] >> r;
  file["T"] >> t;
  if (r.rows == 3 && r.cols == 3 && t.total() == 3) {
    geometry.stereo.rotation_phone_from_laptop = r;
    geometry.stereo.translation_phone_from_laptop_m = t;
  } else
    geometry.stereo.calibrated = false;
  file["baseline_cm"] >> geometry.stereo.baseline_cm;
  file["error_px"] >> geometry.stereo.median_reprojection_error_px;
  std::string health = "degraded";
  if (!file["health"].empty()) file["health"] >> health;
  geometry.stereo.health =
      health == "healthy" ? CalibrationHealth::kHealthy : CalibrationHealth::kDegraded;
  return geometry;
}
}  // namespace dualview
