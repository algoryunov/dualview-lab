#include <algorithm>
#include <cmath>

#include "dualview/hand_tracker.hpp"
namespace dualview {
namespace {
struct Reconstructed {
  Pose points;
  std::vector<double> errors;
  double residual = 1e9;
  float confidence = 0;
};
Reconstructed triangulate(const Hand& a, const Hand& b, const CameraIntrinsics& ca,
                          const CameraIntrinsics& cb, const StereoSolution& stereo) {
  std::vector<cv::Point2f> la(a.landmarks.begin(), a.landmarks.end()),
      lb(b.landmarks.begin(), b.landmarks.end()), ua, ub;
  cv::undistortPoints(la, ua, ca.camera_matrix, ca.distortion_coefficients);
  cv::undistortPoints(lb, ub, cb.camera_matrix, cb.distortion_coefficients);
  cv::Matx34d pa(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0), pb;
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) pb(row, col) = stereo.rotation_phone_from_laptop(row, col);
    pb(row, 3) = stereo.translation_phone_from_laptop_m[row];
  }
  cv::Mat homogeneous;
  cv::triangulatePoints(pa, pb, ua, ub, homogeneous);
  homogeneous.convertTo(homogeneous, CV_64F);
  Reconstructed result;
  double sum = 0;
  for (int i = 0; i < 21; ++i) {
    double w = homogeneous.at<double>(3, i);
    if (std::abs(w) < 1e-8) return {};
    cv::Vec3d p(homogeneous.at<double>(0, i) / w, homogeneous.at<double>(1, i) / w,
                homogeneous.at<double>(2, i) / w);
    cv::Vec3d q = stereo.rotation_phone_from_laptop * p + stereo.translation_phone_from_laptop_m;
    if (!cv::checkRange(cv::Mat(p)) || p[2] < .05 || p[2] > 3 || q[2] < .05) return {};
    std::vector<cv::Point3d> object{cv::Point3d(p)};
    std::vector<cv::Point2d> ap, bp;
    cv::projectPoints(object, cv::Vec3d{}, cv::Vec3d{}, ca.camera_matrix,
                      ca.distortion_coefficients, ap);
    cv::projectPoints(object, stereo.rotation_phone_from_laptop,
                      stereo.translation_phone_from_laptop_m, cb.camera_matrix,
                      cb.distortion_coefficients, bp);
    double error =
        std::max(cv::norm(ap[0] - cv::Point2d(la[i])), cv::norm(bp[0] - cv::Point2d(lb[i])));
    if (!std::isfinite(error)) return {};
    if (error > 15) {
      result.residual = error;
      return result;
    }
    sum += error;
    result.points.push_back(p);
    result.errors.push_back(error);
  }
  result.residual = sum / 21;
  result.confidence = std::min(a.confidence, b.confidence);
  return result;
}
}  // namespace
TrackingResult HandTracker::track(const CameraFrame (&f)[2], const std::vector<Hand>& laptop,
                                  const std::vector<Hand>& phone,
                                  const CameraIntrinsics (&intrinsics)[2],
                                  const StereoSolution& stereo, int phone_offset_ms,
                                  int max_pair_error_ms, bool calibrating, Clock::time_point now) {
  TrackingResult result;
  auto& reason = result.reason;
  reason.clear();
  Pose prior[2];
  for (int i = 0; i < 2; ++i) {
    if (std::chrono::duration<double, std::milli>(now - stable_[i].seen).count() <= pose_grace_ms)
      prior[i] = stable_[i].points;
    const auto& detected = i == 0 ? laptop : phone;
    for (const auto& hand : detected) {
      result.confidences[i].push_back(hand.confidence);
      if (f[i].image.empty()) continue;
      std::vector<cv::Point2d> points;
      for (const auto& p : hand.landmarks)
        points.emplace_back(p.x / f[i].image.cols, p.y / f[i].image.rows);
      result.normalized[i].push_back(std::move(points));
    }
  }
  const auto delta =
      std::abs(std::chrono::duration<double, std::milli>(pairing_time(f[0], 0, phone_offset_ms) -
                                                         pairing_time(f[1], 1, phone_offset_ms))
                   .count());
  if (!f[0].image.empty() && !f[1].image.empty()) {
    result.timing_error_ms = delta;
    result.arrival_delta_ms =
        std::abs(std::chrono::duration<double, std::milli>(f[0].received - f[1].received).count());
  }
  auto a = f[0].image.empty()
               ? std::optional<CameraIntrinsics>{}
               : intrinsics[0].scaled_for_active_frame("laptop", f[0].image.cols, f[0].image.rows);
  auto b = f[1].image.empty()
               ? std::optional<CameraIntrinsics>{}
               : intrinsics[1].scaled_for_active_frame("phone", f[1].image.cols, f[1].image.rows);
  const bool fresh =
      std::chrono::duration<double, std::milli>(now - pairing_time(f[0], 0, phone_offset_ms))
              .count() < 250 &&
      std::chrono::duration<double, std::milli>(now - pairing_time(f[1], 1, phone_offset_ms))
              .count() < 250;
  if (!fresh)
    reason = "stale_frames";
  else if (!stereo.calibrated || !a || !b)
    reason = "calibration_required";
  else if (delta > max_pair_error_ms)
    reason = "frame_pair_out_of_sync";
  else if (laptop.empty() || phone.empty())
    reason = "hand_lost_in_one_or_both_views";
  else {
    std::vector<Reconstructed> matches;
    // Enumerate both assignments, then choose the lowest total reprojection error.
    if (laptop.size() == 2 && phone.size() == 2) {
      auto p = triangulate(laptop[0], phone[0], *a, *b, stereo),
           q = triangulate(laptop[1], phone[1], *a, *b, stereo);
      auto r = triangulate(laptop[0], phone[1], *a, *b, stereo),
           s = triangulate(laptop[1], phone[0], *a, *b, stereo);
      matches = p.residual + q.residual <= r.residual + s.residual ? std::vector{p, q}
                                                                   : std::vector{r, s};
    } else {
      Reconstructed best;
      for (const auto& left : laptop)
        for (const auto& right : phone) {
          auto candidate = triangulate(left, right, *a, *b, stereo);
          if (candidate.residual < best.residual) best = candidate;
        }
      matches.push_back(best);
    }
    // If only the secondary hand remains visible, keep its identity instead of
    // moving the primary slot (and the metal center) to the other palm.
    if (matches.size() == 1 && matches[0].points.size() == 21 && !prior[0].empty() &&
        !prior[1].empty() &&
        cv::norm(matches[0].points[0] - prior[1][0]) <
            cv::norm(matches[0].points[0] - prior[0][0])) {
      matches.insert(matches.begin(), Reconstructed{});
    }
    // Detector order can change between frames. Preserve the primary hand identity
    // so a held object and per-hand filters do not jump to the other palm.
    if (matches.size() == 2 && matches[0].points.size() == 21 && matches[1].points.size() == 21 &&
        !prior[0].empty()) {
      const auto previous = prior[0][0];
      double direct = cv::norm(matches[0].points[0] - previous);
      double swapped = cv::norm(matches[1].points[0] - previous);
      if (!prior[1].empty()) {
        const auto secondary = prior[1][0];
        direct += cv::norm(matches[1].points[0] - secondary);
        swapped += cv::norm(matches[0].points[0] - secondary);
      }
      if (swapped < direct) std::swap(matches[0], matches[1]);
    }
    if (matches[0].residual < 1e9) result.candidate_residual_px = matches[0].residual;
    if (matches[0].residual >= 1e9)
      reason = "stereo_geometry_invalid";
    else if (matches[0].residual > 8)
      reason = "reprojection_residual_too_high";
    else if (matches[0].confidence < .55)
      reason = "tracking_confidence_too_low";
    for (std::size_t i = 0; i < matches.size(); ++i) {
      const auto& match = matches[i];
      if (match.residual > 8 || match.confidence < .55) continue;
      auto& hand = result.hands[i];
      hand.raw = match.points;
      hand.filtered = match.points;
      hand.confidence = match.confidence;
      hand.residuals = match.errors;
      hand.source = PoseSource::Measured;
      hand.hold_age_ms = 0;
      if (prior[i].size() == 21 && cv::norm(prior[i][0] - match.points[0]) < .12)
        for (int j = 0; j < 21; ++j) hand.filtered[j] = prior[i][j] * .35 + match.points[j] * .65;
      cv::Vec3d velocity{};
      auto& stable = stable_[i];
      const double dt = std::chrono::duration<double>(now - stable.seen).count();
      if (!stable.points.empty() && dt >= .015 && dt <= .5) {
        const auto displacement = hand.filtered[0] - stable.points[0];
        if (cv::norm(displacement) < .08) {
          velocity = stable.velocity * .65 + displacement * (.35 / dt);
          const double speed = cv::norm(velocity);
          if (speed > .25) velocity *= .25 / speed;
        }
      }
      stable = {hand.filtered, hand.confidence, velocity, now};
    }
  }
  const bool may_hold =
      fresh && stereo.calibrated && a && b && !calibrating && delta <= max_pair_error_ms;
  for (int i = 0; i < 2; ++i) {
    auto& hand = result.hands[i];
    auto& stable = stable_[i];
    const double elapsed = std::chrono::duration<double, std::milli>(now - stable.seen).count();
    if (may_hold && hand.filtered.empty() && !stable.points.empty() && elapsed <= pose_grace_ms) {
      hand.filtered = stable.points;
      const auto shift = stable.velocity * (.12 * (1 - std::exp(-elapsed / 120.)));
      for (auto& p : hand.filtered) p += shift;
      hand.confidence = stable.confidence;
      hand.source = PoseSource::Predicted;
      hand.hold_age_ms = elapsed;
    }
    if (!may_hold || elapsed > pose_grace_ms) stable = {};
  }
  return result;
}
bool HandTracker::expire(TrackingResult& result, Clock::time_point now) {
  bool expired = false;
  for (int i = 0; i < 2; ++i) {
    auto& hand = result.hands[i];
    if (hand.source == PoseSource::Predicted &&
        std::chrono::duration<double, std::milli>(now - stable_[i].seen).count() > pose_grace_ms) {
      hand = {};
      stable_[i] = {};
      expired = true;
    }
  }
  return expired;
}
namespace {
using Json = nlohmann::json;
Json pose_json(const Pose& points) {
  if (points.empty()) return nullptr;
  Json out = Json::array();
  for (const auto& p : points) out.push_back({p[0], p[1], p[2]});
  return out;
}
Json optional_json(std::optional<double> value) { return value ? Json(*value) : Json(nullptr); }
}  // namespace
Json tracking_json(const TrackingResult& result) {
  Json out = {{"status", result.tracked() ? "tracked" : "unavailable"},
              {"reason", result.reason.empty() ? Json(nullptr) : Json(result.reason)},
              {"timing_error_ms", optional_json(result.timing_error_ms)},
              {"arrival_delta_ms", optional_json(result.arrival_delta_ms)},
              {"candidate_residual_px", optional_json(result.candidate_residual_px)}};
  for (int i = 0; i < 2; ++i) {
    const std::string prefix = i == 0 ? "" : "secondary_", camera = i == 0 ? "laptop" : "phone";
    const auto& h = result.hands[i];
    out[prefix + "confidence"] = h.confidence;
    out[prefix + "source"] = h.source == PoseSource::Measured    ? "measured"
                             : h.source == PoseSource::Predicted ? "predicted"
                                                                 : "missing";
    out[prefix + "hold_age_ms"] = optional_json(h.hold_age_ms);
    out[prefix + "raw_points_m"] = pose_json(h.raw);
    out[prefix + "filtered_points_m"] = pose_json(h.filtered);
    out[prefix + "reprojection_residuals_px"] =
        h.residuals.empty() ? Json(nullptr) : Json(h.residuals);
    out[prefix + "landmark_confidence"] =
        h.raw.empty() ? Json(nullptr) : Json(std::vector<double>(21, h.confidence));
    out[camera + "_confidences"] = result.confidences[i];
    Json normalized = Json::array();
    for (const auto& hand : result.normalized[i]) {
      Json points = Json::array();
      for (const auto& p : hand) points.push_back({p.x, p.y});
      normalized.push_back(points);
    }
    out[camera + "_hands_landmarks_normalized"] = normalized;
    out[camera + "_landmarks_normalized"] = normalized.empty() ? Json(nullptr) : normalized[0];
  }
  return out;
}
}  // namespace dualview
