#include "dualview/interaction.hpp"

#include <algorithm>
#include <cmath>
namespace dualview {
void InteractionController::update(const TrackingResult& h, Clock::time_point now) {
  auto& interaction = state_;
  if (h.hands[0].filtered.size() != 21) {
    stop(h.reason.empty() ? "hand_lost_in_one_or_both_views" : h.reason);
    return;
  }
  const bool holding = h.holding();
  const auto& reason = h.reason;
  // Continue only an existing grab through a brief gap. Predictions cannot
  // start a pinch, switch a model, rotate, or change the two-hand scale.
  if (holding && h.tracked()) {
    if (was_pinching_ && interaction.pinch == true) {
      const auto& predicted = h.hands[0].filtered;
      const auto cursor = (predicted[4] + predicted[8]) * .5;
      interaction.cursor_m = cursor;
      interaction.object_position_m = cursor + grab_offset_;
    }
    interaction.interaction_block_reason = "predicting_last_pose";
    interaction.state = "holding";
    interaction.wrist_twist_active = false;
    interaction.gesture = std::nullopt;
    interaction.hold_elapsed_ms = 0;
    interaction.hold_progress = 0;
    gesture_.clear();
    return;
  }
  interaction.pinch = false;
  interaction.secondary_pinch = false;
  interaction.two_hand_pinch = false;
  interaction.wrist_twist_active = false;
  interaction.secondary_pinch_distance_m = std::nullopt;
  interaction.two_hand_distance_m = std::nullopt;
  interaction.interaction_block_reason =
      reason.empty() ? std::nullopt : std::optional<std::string>(reason);
  if (!reason.empty()) {
    stop(reason);
    return;
  }
  const auto& p = h.hands[0].filtered;
  const double pinch = cv::norm(p[4] - p[8]);
  const bool pinching = pinch < (was_pinching_ ? .055 : .04);
  interaction.pinch = pinching;
  interaction.pinch_distance_m = pinch;
  std::string gesture;
  const auto extended = [&](int tip, int pip) {
    return cv::norm(p[tip] - p[0]) > cv::norm(p[pip] - p[0]) * 1.2;
  };
  if (extended(8, 6) && !extended(12, 10) && !extended(16, 14) && !extended(20, 18))
    gesture = "pointing_up";
  else if (extended(4, 3) && !extended(8, 6) && !extended(12, 10) && !extended(16, 14))
    gesture = "thumb_up";
  if (gesture != gesture_) {
    gesture_ = gesture;
    gesture_since_ = now;
  }
  const double held =
      gesture.empty() ? 0 : std::chrono::duration<double, std::milli>(now - gesture_since_).count();
  interaction.gesture = gesture.empty() ? std::nullopt : std::optional<std::string>(gesture);
  interaction.gesture_confidence = h.hands[0].confidence;
  interaction.hold_elapsed_ms = held;
  interaction.hold_progress = std::min(1., held / 900.);
  const auto cursor = (p[4] + p[8]) * .5;
  interaction.cursor_m = cursor;
  if (pinching) {
    cv::Vec3d current(interaction.object_position_m[0], interaction.object_position_m[1],
                      interaction.object_position_m[2]);
    if (!was_pinching_) grab_offset_ = current - cursor;
    interaction.object_position_m = cursor + grab_offset_;
    const auto across = p[5] - p[17];
    const auto angle = std::atan2(across[1], across[0]);
    if (was_pinching_) {
      auto q = interaction.object_rotation_quat;
      double delta_angle = std::remainder(angle - previous_twist_, 2 * std::acos(-1.));
      double z = q[2], w = q[3], s = std::sin(delta_angle / 2), c = std::cos(delta_angle / 2);
      interaction.object_rotation_quat = {0, 0, z * c + w * s, w * c - z * s};
      interaction.wrist_twist_active = true;
    }
    previous_twist_ = angle;
  }
  if (h.hands[1].filtered.size() == 21) {
    const auto& second = h.hands[1].filtered;
    double gap = cv::norm(second[4] - second[8]);
    interaction.secondary_pinch_distance_m = gap;
    const bool second_pinching = gap < (secondary_was_pinching_ ? .055 : .04);
    interaction.secondary_pinch = second_pinching;
    secondary_was_pinching_ = second_pinching;
    if (pinching && second_pinching) {
      double distance = cv::norm(cursor - (second[4] + second[8]) * .5);
      interaction.two_hand_pinch = true;
      interaction.two_hand_distance_m = distance;
      if (initial_distance_ <= 0) {
        initial_distance_ = distance;
        initial_scale_ = interaction.object_scale_m;
      }
      if (initial_distance_ > .01)
        interaction.object_scale_m =
            std::clamp(initial_scale_ * distance / initial_distance_, .025, .5);
    } else
      initial_distance_ = 0;
  } else {
    initial_distance_ = 0;
    secondary_was_pinching_ = false;
  }
  interaction.state = pinching ? "grabbing" : "idle";
  was_pinching_ = pinching;
}

void InteractionController::stop(const std::string& reason) {
  const auto old = state_;
  state_ = {};
  state_.model_id = old.model_id;
  state_.object_position_m = old.object_position_m;
  state_.object_scale_m = old.object_scale_m;
  state_.object_rotation_quat = old.object_rotation_quat;
  state_.gesture_confidence = old.gesture_confidence;
  state_.interaction_block_reason = reason;
  was_pinching_ = secondary_was_pinching_ = false;
  initial_distance_ = 0;
  gesture_.clear();
  gesture_since_ = {};
  previous_twist_ = 0;
  grab_offset_ = {};
}
void InteractionController::reset() {
  const auto model = state_.model_id;
  *this = InteractionController{};
  state_.model_id = model;
}
void InteractionController::select_model(const std::string& id) {
  if (id != "car" && id != "bed" && id != "flower" && id != "tower")
    throw std::invalid_argument("Unknown model");
  state_.model_id = id;
}
namespace {
using Json = nlohmann::json;
template <class T>
Json optional_json(const std::optional<T>& value) {
  return value ? Json(*value) : Json(nullptr);
}
Json vector(const cv::Vec3d& p) { return {p[0], p[1], p[2]}; }
}  // namespace
Json interaction_json(const InteractionState& s) {
  Json out = {{"state", s.state},
              {"model_id", s.model_id},
              {"gesture", optional_json(s.gesture)},
              {"gesture_confidence", s.gesture_confidence},
              {"hold_progress", s.hold_progress},
              {"hold_elapsed_ms", s.hold_elapsed_ms},
              {"hold_required_ms", 900},
              {"pinch", s.pinch},
              {"secondary_pinch", s.secondary_pinch},
              {"two_hand_pinch", s.two_hand_pinch},
              {"wrist_twist_active", s.wrist_twist_active},
              {"pinch_distance_m", optional_json(s.pinch_distance_m)},
              {"secondary_pinch_distance_m", optional_json(s.secondary_pinch_distance_m)},
              {"two_hand_distance_m", optional_json(s.two_hand_distance_m)},
              {"cursor_m", s.cursor_m ? vector(*s.cursor_m) : Json(nullptr)},
              {"object_position_m", vector(s.object_position_m)},
              {"object_scale_m", s.object_scale_m},
              {"object_rotation_quat",
               {s.object_rotation_quat[0], s.object_rotation_quat[1], s.object_rotation_quat[2],
                s.object_rotation_quat[3]}},
              {"object_status", "draft"},
              {"laptop_object_corners_normalized", nullptr},
              {"phone_object_corners_normalized", nullptr},
              {"detector_reason", nullptr},
              {"events", Json::array()}};
  if (s.interaction_block_reason) out["interaction_block_reason"] = *s.interaction_block_reason;
  return out;
}
}  // namespace dualview
