#include <cassert>

#include "dualview/interaction.hpp"
using namespace dualview;
int main() {
  CameraIntrinsics intrinsics[2];
  StereoSolution stereo;
  HandTracker tracker;
  InteractionController interaction;
  nlohmann::json state;
  TrackingResult latest;
  auto now = Clock::now();
  const auto track = [&](const CameraFrame(&frames)[2], const std::vector<Hand>& laptop,
                         const std::vector<Hand>& phone) {
    const auto result = tracker.track(frames, laptop, phone, intrinsics, stereo, 0, 80, false, now);
    latest = result;
    interaction.update(result, now);
    state["hand_tracking"] = tracking_json(result);
    state["interaction"] = interaction_json(interaction.state());
  };
  for (int i = 0; i < 2; ++i) {
    auto& profile = intrinsics[i];
    profile.camera_id = i == 0 ? "laptop" : "phone";
    profile.width = 640;
    profile.height = 480;
    profile.camera_matrix = {550, 0, 320, 0, 550, 240, 0, 0, 1};
    profile.distortion_coefficients = cv::Mat::zeros(1, 5, CV_64F);
    profile.usable_for_metric_calibration = true;
  }
  stereo.calibrated = true;
  stereo.translation_phone_from_laptop_m = {-.12, 0, 0};
  CameraFrame frames[2];
  const auto advance = [&](int ms) {
    now += std::chrono::milliseconds(ms);
    for (auto& f : frames) f.received = now;
  };
  for (auto& frame : frames) {
    frame.image = cv::Mat::zeros(480, 640, CV_8UC3);
    frame.received = now;
  }
  std::vector<Hand> views[2];
  for (int camera = 0; camera < 2; ++camera) {
    for (int hand = 0; hand < 2; ++hand) {
      Hand detected{};
      detected.confidence = hand == 0 ? .95f : .83f;
      for (int j = 0; j < 21; ++j) {
        double x = (j % 5 - 2) * .012 - camera * .12;
        double y = (j / 5 - 2) * .015 + hand * .08;
        double z = .5 + hand * .25;
        detected.landmarks[j] = {float(550 * x / z + 320), float(550 * y / z + 240)};
      }
      views[camera].push_back(detected);
    }
  }
  track(frames, views[0], views[1]);
  auto tracking = state["hand_tracking"];
  assert(tracking["status"] == "tracked");
  assert(tracking["secondary_confidence"].get<double>() > .8);
  assert(std::abs(tracking["filtered_points_m"][0][2].get<double>() - .5) < .001);
  assert(std::abs(tracking["secondary_filtered_points_m"][0][2].get<double>() - .75) < .001);
  std::swap(views[0][0], views[0][1]);
  std::swap(views[1][0], views[1][1]);
  track(frames, views[0], views[1]);
  assert(state["hand_tracking"]["confidence"].get<double>() > .9);
  assert(std::abs(state["hand_tracking"]["filtered_points_m"][0][2].get<double>() - .5) < .001);
  views[0].resize(1);
  views[1].resize(1);
  track(frames, views[0], views[1]);
  assert(state["hand_tracking"]["source"] == "predicted");
  assert(state["hand_tracking"]["secondary_source"] == "measured");
  assert(std::abs(state["hand_tracking"]["filtered_points_m"][0][2].get<double>() - .5) < .001);
  const auto frozen_object = state["interaction"]["object_position_m"];
  views[1].clear();
  track(frames, views[0], views[1]);
  assert(state["hand_tracking"]["reason"] == "hand_lost_in_one_or_both_views");
  assert(state["hand_tracking"]["phone_confidences"].empty());
  assert(state["hand_tracking"]["status"] == "tracked");
  assert(state["hand_tracking"]["raw_points_m"].is_null());
  assert(state["interaction"]["state"] == "holding");
  assert(state["interaction"]["object_position_m"] == frozen_object);
  // Held data never refreshes the observation timestamp or survives its limit.
  advance(501);
  track(frames, views[0], views[1]);
  assert(state["hand_tracking"]["status"] == "unavailable");
  assert(state["hand_tracking"]["filtered_points_m"].is_null());
  frames[1].received -= std::chrono::milliseconds(120);
  track(frames, views[0], views[1]);
  assert(state["hand_tracking"]["reason"] == "frame_pair_out_of_sync");
  frames[1].received -= std::chrono::seconds(1);
  track(frames, views[0], views[1]);
  assert(state["hand_tracking"]["reason"] == "stale_frames");
  frames[1].image.release();
  track(frames, views[0], views[1]);
  assert(state["hand_tracking"]["timing_error_ms"].is_null());
  frames[1].image = cv::Mat::zeros(480, 640, CV_8UC3);
  // Pinches work immediately, without a separate arming gesture.
  const auto pinch_views = [&](double distance, double gap) {
    for (int camera = 0; camera < 2; ++camera) {
      frames[camera].received = now;
      views[camera].clear();
      for (int hand = 0; hand < 2; ++hand) {
        Hand detected{};
        detected.confidence = .95f;
        for (int j = 0; j < 21; ++j) {
          double x = (j % 5 - 2) * .012;
          double y = (j / 5 - 2) * .015 + (hand == 0 ? -distance / 2 : distance / 2);
          if (j == 4 || j == 8) {
            x = j == 4 ? 0 : gap;
            y = hand == 0 ? -distance / 2 : distance / 2;
          }
          detected.landmarks[j] = {float(550 * (x - camera * .12) / .6 + 320),
                                   float(550 * y / .6 + 240)};
        }
        views[camera].push_back(detected);
      }
    }
    track(frames, views[0], views[1]);
  };
  pinch_views(.12, .02);
  assert(state["interaction"]["state"] == "grabbing");
  assert(state["interaction"]["two_hand_pinch"] == true);
  pinch_views(.24, .02);
  const double expanded = state["interaction"]["object_scale_m"];
  assert(expanded > .15);
  // A short missed detection freezes the scale and keeps the original grab.
  track(frames, {}, {});
  assert(state["interaction"]["object_scale_m"] == expanded);
  pinch_views(.24, .02);
  assert(state["interaction"]["state"] == "grabbing");
  pinch_views(.08, .02);
  assert(state["interaction"]["object_scale_m"].get<double>() < expanded);
  interaction.reset();
  state["interaction"] = interaction_json(interaction.state());
  pinch_views(.08, .02);
  assert(std::abs(state["interaction"]["object_scale_m"].get<double>() - .11) < .001);
  pinch_views(.08, .045);
  assert(state["interaction"]["secondary_pinch"] == true);
  for (int i = 0; i < 4; ++i) pinch_views(.08, .09);
  assert(state["interaction"]["two_hand_pinch"] == false);
  // Smooth translation continues an existing grab across a missed stereo result.
  tracker.reset();
  interaction.stop("phone_disconnected");
  state["interaction"] = interaction_json(interaction.state());
  pinch_views(.12, .02);
  advance(100);
  for (auto& camera : views)
    for (auto& detected : camera)
      for (auto& landmark : detected.landmarks) landmark.x += 11;
  track(frames, views[0], views[1]);
  const double before_gap = state["interaction"]["object_position_m"][0];
  advance(250);
  const auto hold_age = state["hand_tracking"]["hold_age_ms"];
  track(frames, {}, {});
  assert(state["hand_tracking"]["source"] == "predicted");
  const double during_gap = state["interaction"]["object_position_m"][0];
  assert(during_gap > before_gap && during_gap - before_gap <= .03);
  assert(state["hand_tracking"]["raw_points_m"].is_null());
  assert(state["interaction"]["gesture"].is_null());
  assert(state["interaction"]["wrist_twist_active"] == false);
  track(frames, {}, {});
  assert(state["hand_tracking"]["hold_age_ms"] >= hold_age);
  advance(251);
  track(frames, {}, {});
  assert(state["hand_tracking"]["status"] == "unavailable");
  assert(state["interaction"]["pinch"] == false);

  // Invalid timing must clear prediction even inside the grace period.
  pinch_views(.12, .02);
  frames[1].received -= std::chrono::milliseconds(100);
  track(frames, {}, {});
  assert(state["hand_tracking"]["status"] == "unavailable");
  assert(state["hand_tracking"]["filtered_points_m"].is_null());
  tracker.reset();
  interaction.stop("phone_disconnected");
  state["interaction"] = interaction_json(interaction.state());
  assert(state["hand_tracking"]["filtered_points_m"].is_null());
  assert(state["interaction"]["state"] == "idle");
  // Expire predictions on an idle worker tick without refreshing observations.
  pinch_views(.12, .02);
  track(frames, {}, {});
  assert(latest.hands[0].source == PoseSource::Predicted);
  advance(499);
  assert(!tracker.expire(latest, now));
  advance(2);
  assert(tracker.expire(latest, now) && !latest.tracked());
}
