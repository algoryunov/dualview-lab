#include "dualview/interaction.hpp"

#include <cassert>
using namespace dualview;
int main() {
  InteractionController controller;
  TrackingResult result;
  result.reason.clear();
  result.hands[0].filtered = Pose(21, cv::Vec3d{0, 0, .5});
  result.hands[0].filtered[8] = {.02, 0, .5};
  result.hands[0].filtered[5] = {.02, .02, .5};
  result.hands[0].filtered[17] = {-.02, -.02, .5};
  result.hands[0].confidence = .9;
  result.hands[0].source = PoseSource::Predicted;
  controller.update(result);
  assert(!controller.state().pinch);  // Predictions cannot initiate a grab.
  result.hands[0].source = PoseSource::Measured;
  controller.update(result);
  assert(controller.state().pinch);
  for (auto& p : result.hands[0].filtered) p[0] += .03;
  controller.update(result);
  controller.select_model("flower");
  const auto position = controller.state().object_position_m;
  const auto rotation = controller.state().object_rotation_quat;
  controller.stop("phone_disconnected");
  assert(controller.state().object_position_m == position);
  assert(controller.state().object_rotation_quat == rotation);
  assert(!controller.state().cursor_m && !controller.state().pinch);
  assert(controller.state().model_id == "flower");
  controller.reset();
  assert(controller.state().model_id == "flower");
  assert(controller.state().object_position_m == cv::Vec3d(0, 0, .45));
  assert(controller.state().object_rotation_quat == cv::Vec4d(0, 0, 0, 1));
  assert(controller.state().object_scale_m == .11);
  auto json = interaction_json(controller.state());
  assert(json["gesture"].is_null() && json["cursor_m"].is_null());
  assert(json["events"].is_array() && json["hold_required_ms"] == 900);
}
