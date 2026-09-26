#pragma once
#include "dualview/hand_tracker.hpp"
namespace dualview {
struct InteractionState {
  std::string state = "idle", model_id = "car";
  std::optional<std::string> gesture, interaction_block_reason;
  double gesture_confidence = 0, hold_progress = 0, hold_elapsed_ms = 0;
  bool pinch = false, secondary_pinch = false, two_hand_pinch = false, wrist_twist_active = false;
  std::optional<double> pinch_distance_m, secondary_pinch_distance_m, two_hand_distance_m;
  std::optional<cv::Vec3d> cursor_m;
  cv::Vec3d object_position_m{0, 0, .45};
  double object_scale_m = .11;
  cv::Vec4d object_rotation_quat{0, 0, 0, 1};
};
nlohmann::json interaction_json(const InteractionState& state);
class InteractionController {
 public:
  const InteractionState& state() const { return state_; }
  void update(const TrackingResult& tracking, Clock::time_point now = Clock::now());
  void stop(const std::string& reason);
  void reset();
  void select_model(const std::string& id);

 private:
  InteractionState state_;
  Clock::time_point gesture_since_{};
  std::string gesture_;
  double initial_distance_ = 0, initial_scale_ = .11, previous_twist_ = 0;
  bool was_pinching_ = false, secondary_was_pinching_ = false;
  cv::Vec3d grab_offset_{};
};
}  // namespace dualview
