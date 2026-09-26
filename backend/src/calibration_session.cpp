#include "dualview/calibration_session.hpp"

#include <algorithm>
#include <filesystem>
namespace dualview {
namespace {
double age(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
}  // namespace
void CalibrationSession::start(CalibrationMode requested, cv::Size size) {
  if (active()) throw std::runtime_error("Calibration is already running");
  if (requested == CalibrationMode::Idle) throw std::invalid_argument("Invalid calibration mode");
  mode = requested;
  calibration_size_ = size;
  intrinsic_observations_.clear();
  stereo_observations_.clear();
  calibration_started_ = Clock::now();
  calibration_last_ = {};
  calibration_sequences_[0] = calibration_sequences_[1] = 0;
  progress = {
      {"status", "capturing"},
      {"accepted_views", 0},
      {"rejected_views", 0},
      {"required_views", stereo() ? 20 : 25},
      {"elapsed_ms", 0},
      {"last_rejection", nullptr},
      {"message", stereo() ? "Keep cameras fixed and show the target to both."
                           : "Move the printed target through different angles and positions."}};
  if (stereo())
    progress["health"] = "unavailable";
  else
    progress["camera_id"] = requested == CalibrationMode::Laptop ? "laptop" : "phone";
}
bool CalibrationSession::process(const CameraFrame (&f)[2], CameraIntrinsics (&intrinsics_)[2],
                                 StereoSolution& stereo_, int phone_offset_ms) {
  bool changed = false;
  if (mode == CalibrationMode::Idle || age(calibration_last_) < 500) return false;
  calibration_last_ = Clock::now();
  auto& active = progress;
  active["elapsed_ms"] = age(calibration_started_);
  CharucoSpec spec;
  if (mode != CalibrationMode::Stereo) {
    int index = mode == CalibrationMode::Laptop ? 0 : 1;
    auto& progress = this->progress;
    if (f[index].image.empty() || age(f[index].received) > 650) {
      progress["last_rejection"] = "Waiting for fresh camera frames.";
      return false;
    }
    if (f[index].image.size() != calibration_size_)
      throw std::runtime_error(
          "Camera resolution changed during intrinsic capture; restart calibration");
    if (f[index].sequence == calibration_sequences_[index]) {
      progress["last_rejection"] = "Waiting for a new camera frame.";
      return false;
    }
    calibration_sequences_[index] = f[index].sequence;
    const auto observation = detect_intrinsic_observation(f[index].image, spec);
    if (!observation) {
      progress["rejected_views"] = progress["rejected_views"].get<int>() + 1;
      progress["last_rejection"] = "Target not detected clearly enough.";
      return false;
    }
    const bool duplicate = std::ranges::any_of(intrinsic_observations_, [&](const auto& earlier) {
      return cv::norm(earlier.center_px - observation->center_px) <
                 std::hypot(f[index].image.cols, f[index].image.rows) * .025 &&
             std::abs(earlier.coverage - observation->coverage) < .015;
    });
    if (duplicate) {
      progress["rejected_views"] = progress["rejected_views"].get<int>() + 1;
      progress["message"] = "Move the target to a new position, distance, or angle.";
      progress["last_rejection"] = "Target position is too similar to an accepted view.";
      return false;
    }
    intrinsic_observations_.push_back(*observation);
    progress["accepted_views"] = intrinsic_observations_.size();
    progress["last_rejection"] = nullptr;
    if (intrinsic_observations_.size() < 25) return false;
    std::string error;
    const auto solution =
        solve_intrinsics(intrinsic_observations_, f[index].image.size(), spec, &error);
    if (!solution) {
      mode = CalibrationMode::Idle;
      progress["status"] = "failed";
      progress["message"] = error;
      return false;
    }
    intrinsics_[index] = {index == 0 ? "laptop" : "phone",
                          "default",
                          f[index].image.cols,
                          f[index].image.rows,
                          solution->camera_matrix,
                          solution->distortion_coefficients,
                          true};
    changed = true;
    mode = CalibrationMode::Idle;
    progress.update({{"status", "completed"},
                     {"message", "Camera profile saved."},
                     {"reprojection_error_px", solution->rms_reprojection_error_px}});
  } else {
    if (f[0].image.empty() || f[1].image.empty() || age(f[0].received) > 650 ||
        age(f[1].received) > 650) {
      active["last_rejection"] = "Waiting for fresh frames from both cameras.";
      return false;
    }
    const auto delta =
        std::abs(std::chrono::duration<double, std::milli>(pairing_time(f[0], 0, phone_offset_ms) -
                                                           pairing_time(f[1], 1, phone_offset_ms))
                     .count());
    auto& progress = this->progress;
    if (f[0].sequence == calibration_sequences_[0] || f[1].sequence == calibration_sequences_[1]) {
      progress["last_rejection"] = "Waiting for a new frame from each camera.";
      return false;
    }
    for (int i = 0; i < 2; ++i) calibration_sequences_[i] = f[i].sequence;
    auto a = intrinsics_[0].scaled_for_active_frame("laptop", f[0].image.cols, f[0].image.rows);
    auto b = intrinsics_[1].scaled_for_active_frame("phone", f[1].image.cols, f[1].image.rows);
    if (!a || !b) throw std::runtime_error("Camera mode changed; recalibrate intrinsics");
    const auto p = detect_target_pose(f[0].image, *a, spec),
               q = detect_target_pose(f[1].image, *b, spec);
    // Keep calibration capture stricter than interactive tracking.
    if (delta > 80 || !p || !q) {
      progress["rejected_views"] = progress["rejected_views"].get<int>() + 1;
      progress["last_rejection"] = delta > 80
                                       ? "Camera pair timing exceeds 80 ms. Keep the target still."
                                   : !p ? "Target not detected in the laptop view."
                                        : "Target not detected in the phone view.";
      return false;
    }
    const auto [r, t] = relative_transform(*p, *q);
    stereo_observations_.push_back(
        {r, t, std::max(p->reprojection_error_px, q->reprojection_error_px), delta});
    progress["accepted_views"] = stereo_observations_.size();
    progress["last_rejection"] = nullptr;
    if (stereo_observations_.size() < 20) return false;
    stereo_ = solve_stereo_observations(stereo_observations_);
    changed = true;
    mode = CalibrationMode::Idle;
    progress.update(
        {{"status", stereo_.calibrated ? "calibrated" : "failed"},
         {"health", stereo_.health == CalibrationHealth::kHealthy ? "healthy" : "degraded"},
         {"message", stereo_.message},
         {"baseline_cm", stereo_.baseline_cm},
         {"median_reprojection_error_px", stereo_.median_reprojection_error_px},
         {"median_stereo_reprojection_error_px", stereo_.median_reprojection_error_px},
         {"median_pairing_error_ms", stereo_.median_pairing_error_ms}});
  }
  return changed;
}
}  // namespace dualview
