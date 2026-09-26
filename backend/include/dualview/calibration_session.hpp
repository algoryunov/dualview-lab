#pragma once
#include <nlohmann/json.hpp>

#include "dualview/calibration.hpp"
#include "dualview/synchronized_frames.hpp"
namespace dualview {
enum class CalibrationMode { Idle, Laptop, Phone, Stereo };
class CalibrationSession {
 public:
  bool active() const { return mode != CalibrationMode::Idle; }
  bool uses_phone() const {
    return mode == CalibrationMode::Phone || mode == CalibrationMode::Stereo;
  }
  bool stereo() const { return mode == CalibrationMode::Stereo; }
  double elapsed_ms() const {
    return std::chrono::duration<double, std::milli>(Clock::now() - calibration_started_).count();
  }
  void start(CalibrationMode requested, cv::Size size = {});
  void cancel() {
    mode = CalibrationMode::Idle;
    intrinsic_observations_.clear();
    stereo_observations_.clear();
  }
  bool process(const CameraFrame (&frames)[2], CameraIntrinsics (&intrinsics)[2],
               StereoSolution& stereo, int phone_offset_ms);
  nlohmann::json progress;

 private:
  CalibrationMode mode = CalibrationMode::Idle;
  cv::Size calibration_size_{};
  std::vector<IntrinsicObservation> intrinsic_observations_;
  std::vector<CalibrationObservation> stereo_observations_;
  Clock::time_point calibration_last_{};
  Clock::time_point calibration_started_{};
  std::uint64_t calibration_sequences_[2]{};
};
struct CalibrationGeometry {
  CameraIntrinsics intrinsics[2];
  StereoSolution stereo;
};
CalibrationGeometry read_calibration(const std::string& path);
void save_calibration(const std::string& path, const CameraIntrinsics (&intrinsics)[2],
                      const StereoSolution& stereo);
}  // namespace dualview
