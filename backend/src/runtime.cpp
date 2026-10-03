#include "dualview/runtime.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <opencv2/videoio.hpp>

#include "dualview/event_log.hpp"
#include "dualview/pose_capture.hpp"
namespace dualview {
namespace {
Config startup_config(Config config) {
  config.calibration_file =
      std::filesystem::absolute(config.calibration_file).lexically_normal().string();
  return config;
}
double age(Clock::time_point time) {
  return std::chrono::duration<double, std::milli>(Clock::now() - time).count();
}
Json initial_state() {
  return {{"phone_state", "waiting"},
          {"calibration",
           {{"status", "not_started"},
            {"health", "unavailable"},
            {"message", "Calibrate each camera, then the fixed pair."},
            {"accepted_views", 0},
            {"required_views", 20},
            {"rejected_views", 0},
            {"baseline_cm", nullptr},
            {"median_reprojection_error_px", nullptr},
            {"median_pairing_error_ms", nullptr}}},
          {"intrinsics",
           {{"camera_id", ""},
            {"status", "not_started"},
            {"message", "Create an intrinsic profile for each camera."},
            {"accepted_views", 0},
            {"rejected_views", 0},
            {"required_views", 25},
            {"reprojection_error_px", nullptr}}},
          {"processing",
           {{"status", "latest_frame_wins"},
            {"calibration_file", ""},
            {"calibration_loaded", false},
            {"max_pair_error_ms", 80},
            {"hand_inference_duration_ms", nullptr},
            {"hand_inference_fps", nullptr},
            {"max_concurrency", 1}}}};
}
Json matrix(const cv::Matx33d& m) {
  return {{m(0, 0), m(0, 1), m(0, 2)}, {m(1, 0), m(1, 1), m(1, 2)}, {m(2, 0), m(2, 1), m(2, 2)}};
}
Json vec(const cv::Vec3d& v) { return {v[0], v[1], v[2]}; }
}  // namespace
Runtime::Runtime(const Config& config)
    : config_(startup_config(config)),
      active_phone_offset_ms_(config.phone_time_offset_ms),
      metadata_(initial_state()),
      metal_(config.metal_log) {
  metadata_["processing"]["calibration_file"] = config_.calibration_file;
  metadata_["processing"]["max_pair_error_ms"] = config_.max_pair_error_ms;
  tracker_.configure(config_.tracking_filter, config_.tracking_filter_compare,
                     config_.filter_tuning);
  metadata_["processing"]["tracking_filter"] = filter_strategy_name(config_.tracking_filter);
  metadata_["processing"]["tracking_filter_compare"] = config_.tracking_filter_compare;
  metadata_["processing"]["phone_time_offset_ms"] = active_phone_offset_ms_;
  if (config_.inference_enabled) {
    inference_ = std::make_unique<inference::Controller>(
        inference::default_registry(), config_.provider, config_.allow_cpu_fallback);
    hands_ = std::make_unique<HandPipeline>(*inference_, config_.models);
    metadata_["processing"]["hand_preprocessing"] = "opencv-zoo-roi-v1";
    metadata_["processing"]["provider"] = inference_->info().id;
    metadata_["processing"]["execution_provider"] = inference_->info().device;
    metadata_["processing"]["fallback_reason"] = inference_->fallback_reason();
  } else {
    metadata_["processing"]["provider"] = "disabled";
  }
  load_calibration();
  if (config_.camera_enabled)
    capture_ = std::jthread([this](std::stop_token stop) {
      cv::VideoCapture camera(config_.camera_index);
      if (!camera.isOpened()) {
        std::cerr << "Laptop camera unavailable\n";
        return;
      }
      camera.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
      camera.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
      while (!stop.stop_requested()) {
        cv::Mat image;
        if (!camera.read(image)) break;
        frames_.publish(0, {image, Clock::now(), 0, {}, {}});
      }
    });
  processing_ = std::jthread([this](std::stop_token stop) { process(stop); });
}
Runtime::~Runtime() {
  capture_.request_stop();
  processing_.request_stop();
  if (capture_.joinable()) capture_.join();
  if (processing_.joinable()) processing_.join();
}
void Runtime::receive_phone(const cv::Mat& image, std::optional<double> pts_ms,
                            std::optional<double> receiver_lateness_ms) {
  const auto generation = frames_.phone_generation();
  const auto received = Clock::now();
  frames_.publish(1, {image.clone(), received, 0, pts_ms, receiver_lateness_ms}, generation);
}
void Runtime::transport_diagnostics(const Json& values) {
  std::lock_guard lock(mutex_);
  Json clean = Json::object();
  for (const auto* key :
       {"packets_lost", "jitter_ms", "round_trip_ms", "jitter_buffer_ms", "frames_dropped",
        "frames_decoded", "bytes_received", "phone_encode_ms", "clock_sync_ready",
        "clock_probe_rtt_ms", "clock_uncertainty_ms", "sender_reference_packets"}) {
    if (values.contains(key) && values[key].is_number()) {
      const double v = values[key].get<double>();
      if (std::isfinite(v) && v >= 0 && v < 1e15) clean[key] = v;
    }
  }
  auto& diagnostics = metadata_["processing"]["phone_transport"];
  if (!diagnostics.is_object()) diagnostics = Json::object();
  diagnostics.update(clean);
}
void Runtime::enqueue_event(Json event) {
  transport_events_.push_back(std::move(event));
  while (transport_events_.size() > 32) transport_events_.pop_front();
}
void Runtime::transport_event(const std::string& reason) {
  std::lock_guard lock(mutex_);
  const auto text = reason.substr(0, 180);
  metadata_["processing"]["phone_connection_event"] = text;
  enqueue_event({{"event", "phone_connection"}, {"reason", text}});
  while (transport_events_.size() > 32) transport_events_.pop_front();
}
void Runtime::clear_phone() {
  std::lock_guard persistence(persistence_mutex_);
  std::lock_guard lock(mutex_);
  if (calibration_.uses_phone()) {
    metadata_[calibration_.stereo() ? "calibration" : "intrinsics"]["status"] = "not_started";
    calibration_.cancel();
    revisions_.calibration_changed();
  }
  revisions_.phone_replaced();
  active_phone_offset_ms_ = config_.phone_time_offset_ms;
  metadata_["processing"]["phone_time_offset_ms"] = active_phone_offset_ms_;
  diagnostics_.reset();
  frames_.clear_phone();
  metadata_["processing"]["phone_transport"] = Json::object();
  metadata_["phone_state"] = "disconnected";
  tracking_ = {};
  tracker_.reset();
  stop_interaction("phone_disconnected");
}
void Runtime::stop_interaction(const std::string& reason) { interaction_.stop(reason); }
CameraFrame Runtime::frame(CameraId camera) const {
  return frames_.latest().at(static_cast<std::size_t>(camera));
}
Json Runtime::snapshot() const {
  std::unique_lock lock(mutex_);
  const auto frames_ = this->frames_.latest();
  auto result = metadata_;
  result["processing"].update(diagnostics_.snapshot());
  result["processing"]["discarded_work"] = discarded_work_;
  const auto tracking = tracking_;
  const auto interaction = interaction_.state();
  const CameraIntrinsics intrinsics_[2] = {this->intrinsics_[0], this->intrinsics_[1]};
  const auto stereo = this->stereo_;
  if (!frames_[1].image.empty()) result["phone_state"] = "connected";
  if (calibration_.active())
    result[calibration_.stereo() ? "calibration" : "intrinsics"]["elapsed_ms"] =
        calibration_.elapsed_ms();
  lock.unlock();
  result["hand_tracking"] = tracking_json(tracking);
  result["interaction"] = interaction_json(interaction);
  for (int i = 0; i < 2; ++i) {
    const auto& f = frames_[i];
    result[i == 0 ? "laptop" : "phone"] = {
        {"available", !f.image.empty() && age(f.received) < 650},
        {"width", f.image.empty() ? Json(nullptr) : Json(f.image.cols)},
        {"height", f.image.empty() ? Json(nullptr) : Json(f.image.rows)},
        {"age_ms", f.image.empty() ? Json(nullptr) : Json(age(f.received))}};
    const auto& profile = intrinsics_[i];
    const std::string camera = i == 0 ? "laptop" : "phone";
    const bool live = !f.image.empty() && age(f.received) < 650;
    std::string compatibility = "missing", detail;
    if (profile.valid()) {
      compatibility = !live ? "waiting_for_camera"
                      : profile.scaled_for_active_frame(camera, f.image.cols, f.image.rows, &detail)
                          ? "compatible"
                          : "mode_mismatch";
    }
    result["intrinsics"]["profiles"][camera] = {{"available", profile.valid()},
                                                {"width", profile.width},
                                                {"height", profile.height},
                                                {"compatibility", compatibility},
                                                {"detail", detail}};
  }
  const auto metrics = this->frames_.metrics();
  result["processing"]["frame_input"] = {
      {"history_depth", {metrics.depth[0], metrics.depth[1]}},
      {"evicted_frames", {metrics.evicted[0], metrics.evicted[1]}},
      {"max_lock_wait_us", metrics.max_lock_wait_us}};
  result["processing"].update(metal_.status());
  result["cameras"] = cameras(frames_, intrinsics_, stereo);
  return result;
}
CommandReply Runtime::execute(const RuntimeCommand& request) {
  // Serialize file commits with calibration replacement/cancellation, without holding the state
  // lock during I/O.
  std::unique_lock persistence(persistence_mutex_, std::defer_lock);
  if (std::holds_alternative<StartStereo>(request) ||
      std::holds_alternative<StartIntrinsics>(request) ||
      std::holds_alternative<CancelCalibration>(request))
    persistence.lock();
  std::unique_lock lock(mutex_);
  const auto frames_ = this->frames_.latest();
  if (const auto* command = std::get_if<SetDiagnosticPhase>(&request)) {
    const std::string phase = phase_name(command->phase);
    if (phase != "idle" && (frames_[0].image.empty() || frames_[1].image.empty() ||
                            age(frames_[0].received) > 250 || age(frames_[1].received) > 250))
      throw std::invalid_argument("Both cameras must deliver fresh frames");
    diagnostics_.start(command->phase);
    enqueue_event({{"event", "diagnostic_phase"}, {"phase", phase}});
    return {command->phase, {}};
  }
  const bool apply_timing = std::holds_alternative<ApplyTiming>(request);
  if (apply_timing || std::holds_alternative<ResetTiming>(request)) {
    if (diagnostics_.active() || calibration_.active())
      throw std::invalid_argument("Finish the active test or calibration first");
    int offset = 0;
    if (apply_timing) offset = diagnostics_.suggested_offset();
    if (apply_timing && (frames_[0].image.empty() || frames_[1].image.empty() ||
                         age(frames_[0].received) > 250 || age(frames_[1].received) > 250))
      throw std::invalid_argument("Both cameras must deliver fresh frames");
    active_phone_offset_ms_ = offset;
    revisions_.timing_changed();
    metadata_["processing"]["phone_time_offset_ms"] = offset;
    tracking_ = {};
    tracker_.reset();
    stop_interaction("timing_changed");
    enqueue_event({{"event", "timing_offset"}, {"offset_ms", offset}});
    if (apply_timing) {
      diagnostics_.start(DiagnosticPhase::Moving);
      enqueue_event({{"event", "diagnostic_phase"}, {"phase", "moving"}, {"offset_ms", offset}});
    }
    return {{}, offset};
  }
  if (std::holds_alternative<MetalSample>(request)) {
    auto clean = std::get<MetalSample>(request).values;
    const auto tracking = tracking_;
    const auto interaction = interaction_.state();
    lock.unlock();
    metal_.record(std::move(clean), tracking, interaction);
    return {};
  } else if (std::holds_alternative<StartIntrinsics>(request) ||
             std::holds_alternative<StartStereo>(request)) {
    if (calibration_.active()) throw std::runtime_error("Calibration is already running");
    CalibrationMode mode = CalibrationMode::Stereo;
    cv::Size size;
    if (const auto* command = std::get_if<StartIntrinsics>(&request)) {
      const int index = command->camera == CameraId::Laptop ? 0 : 1;
      if (frames_[index].image.empty() || age(frames_[index].received) > 650)
        throw std::runtime_error("Camera is not live");
      mode = index == 0 ? CalibrationMode::Laptop : CalibrationMode::Phone;
      size = frames_[index].image.size();
    } else if (!intrinsics_[0].valid() || !intrinsics_[1].valid()) {
      throw std::runtime_error("Calibrate both camera intrinsics first");
    }
    calibration_.start(mode, size);
    revisions_.calibration_changed();
    stereo_.calibrated = false;
    metadata_["calibration"]["status"] = "not_started";
    metadata_["calibration"]["health"] = "unavailable";
    metadata_[calibration_.stereo() ? "calibration" : "intrinsics"].update(calibration_.progress);
    tracker_.reset();
    tracking_ = {};
    tracking_.reason = "calibration_required";
    stop_interaction("calibration_required");
  } else if (std::holds_alternative<CancelCalibration>(request)) {
    calibration_.cancel();
    revisions_.calibration_changed();
    metadata_["intrinsics"]["status"] = "not_started";
    metadata_["calibration"]["status"] = "not_started";
  } else if (std::holds_alternative<SelectModel>(request)) {
    interaction_.select_model(std::get<SelectModel>(request).id);
  } else if (std::holds_alternative<ResetInteraction>(request)) {
    interaction_.reset();
  } else
    throw std::logic_error("Unhandled runtime command");
  return {};
}
// The worker runs three phases per iteration, and the phase boundaries are the
// concurrency contract rather than a stylistic split:
//
//   1. Snapshot. Under mutex_, copy every input the iteration needs (frames,
//      intrinsics, stereo geometry, tracker history, calibration session) plus a
//      WorkRevisions token identifying the phone/timing/calibration state they
//      came from.
//   2. Compute. Release mutex_ and run inference, ChArUco detection/solving,
//      triangulation and filtering on those private copies. Nothing here may
//      touch shared state, because commands run concurrently.
//   3. Validate and publish. Retake mutex_ and commit only if revisions_ still
//      accepts the token; otherwise the inputs were invalidated mid-computation
//      (phone replaced, timing reset, calibration cancelled) and the result is
//      discarded. Saving geometry releases mutex_ for disk I/O, so it revalidates
//      a second time and re-checks frame age afterwards -- a slow save must never
//      republish an old pose as a fresh measurement.
//
// Keeping this in one function keeps the lock/revision protocol visible in
// reading order. See docs/runtime-ownership.md for the full contract.
void Runtime::process(std::stop_token stop) {
  EventLog log(config_.tracking_log);
  PoseCapture capture(config_.tracking_capture, config_.tracking_capture_seconds);
  if (capture.enabled()) {
    std::lock_guard lock(mutex_);
    capture.begin({{"tracking_filter", filter_strategy_name(config_.tracking_filter)},
                   {"tracking_filter_compare", config_.tracking_filter_compare},
                   {"filter_alpha", config_.filter_tuning.alpha},
                   {"filter_beta", config_.filter_tuning.beta},
                   {"kalman_accel_sigma", config_.filter_tuning.kalman_accel_sigma},
                   {"kalman_noise_px", config_.filter_tuning.kalman_noise_px},
                   {"max_pair_error_ms", config_.max_pair_error_ms},
                   {"inference_fps", config_.inference_fps},
                   {"provider", metadata_["processing"].value("provider", "")},
                   {"baseline_cm", stereo_.baseline_cm},
                   {"calibrated", stereo_.calibrated},
                   {"median_reprojection_error_px", stereo_.median_reprojection_error_px}});
  }
  std::string last_status;
  Clock::time_point last_log{};
  std::uint64_t losses = 0, recoveries = 0;
  bool was_tracking = false;
  std::uint64_t previous[2]{};
  std::uint64_t previous_timing = 0;
  std::uint64_t cache_epoch = 0, inference_calls = 0, cache_hits = 0, pair_count = 0;
  DetectionCache<std::vector<Hand>> cache[2];
  Json reason_counts = Json::object();
  while (!stop.stop_requested()) {
    const auto started = Clock::now();
    bool processed = false;
    CameraFrame f[2];
    WorkRevisions::Token revision;
    std::uint64_t diagnostic_generation;
    int phone_offset;
    CalibrationSession calibration;
    CameraIntrinsics intrinsics[2];
    StereoSolution stereo;
    HandTracker tracker;
    {
      std::lock_guard lock(mutex_);
      revision = revisions_.capture();
      diagnostic_generation = diagnostics_.generation();
      calibration = calibration_;
      intrinsics[0] = intrinsics_[0];
      intrinsics[1] = intrinsics_[1];
      stereo = stereo_;
      tracker = tracker_;
      phone_offset = active_phone_offset_ms_;
      if (previous_timing != revision.timing) {
        previous[0] = previous[1] = 0;
        previous_timing = revision.timing;
      }
      const auto selected =
          frames_.select(started, active_phone_offset_ms_, config_.max_pair_error_ms, previous);
      f[0] = selected[0];
      f[1] = selected[1];
      metadata_["processing"]["timing_basis"] =
          active_phone_offset_ms_ == 0 ? "receiver_arrival" : "receiver_arrival_with_manual_offset";
      metadata_["processing"]["capture_sync_verified"] = false;
      metadata_["processing"]["phone_time_offset_ms"] = active_phone_offset_ms_;
      metadata_["processing"]["pair_max_age_ms"] = 250;
      metadata_["processing"]["pair_selection"] = "closest_to_slower_latest";
    }
    if (cache_epoch != revision.phone) {
      cache_epoch = revision.phone;
      previous[1] = 0;
    }
    if (previous[0] != f[0].sequence || previous[1] != f[1].sequence) {
      previous[0] = f[0].sequence;
      previous[1] = f[1].sequence;
      processed = bool(hands_);
      try {
        std::vector<Hand> detected[2];
        if (hands_)
          for (int i = 0; i < 2; ++i)
            if (!f[i].image.empty() && age(f[i].received) < 650) {
              bool hit = false;
              detected[i] = cache[i].get(
                  f[i].sequence, i == 1 ? revision.phone : 0,
                  [&] { return hands_->detect(f[i].image); }, hit);
              if (hit)
                ++cache_hits;
              else
                ++inference_calls;
            }
        const auto geometry_started = Clock::now();
        const bool was_calibrating = calibration.active();
        const bool stereo_session = calibration.stereo();
        const bool geometry_changed = calibration.process(f, intrinsics, stereo, phone_offset);
        auto tracking = tracker.track(f, detected[0], detected[1], intrinsics, stereo, phone_offset,
                                      config_.max_pair_error_ms, calibration.active());
        const double geometry_ms = age(geometry_started);
        std::unique_lock persistence(persistence_mutex_, std::defer_lock);
        if (geometry_changed) persistence.lock();
        std::unique_lock lock(mutex_);
        // Never publish inference from a phone connection that has been replaced.
        if (!revisions_.accepts(revision)) {
          ++discarded_work_;
          continue;
        }
        if (geometry_changed) {
          lock.unlock();
          save_calibration(config_.calibration_file, intrinsics, stereo);
          // Persistence can take longer than a frame budget; never label that pose fresh.
          if (age(pairing_time(f[0], 0, phone_offset)) >= 250 ||
              age(pairing_time(f[1], 1, phone_offset)) >= 250) {
            tracking = {};
            tracking.reason = "stale_frames";
            tracker.reset();
          }
          lock.lock();
          if (!revisions_.accepts(revision)) {
            ++discarded_work_;
            continue;
          }
        }
        diagnostics_.collect(diagnostic_generation, f, detected);
        calibration_ = std::move(calibration);
        intrinsics_[0] = intrinsics[0];
        intrinsics_[1] = intrinsics[1];
        stereo_ = stereo;
        if (was_calibrating)
          metadata_[stereo_session ? "calibration" : "intrinsics"].update(calibration_.progress);
        tracker_ = std::move(tracker);
        tracking_ = std::move(tracking);
        interaction_.update(tracking_);
        ++pair_count;
        const std::string key = tracking_.reason.empty() ? "measured" : tracking_.reason;
        reason_counts[key] = reason_counts.value(key, std::uint64_t{0}) + 1;
        metadata_["processing"]["reason_counts"] = reason_counts;
        diagnostics_.observe(key);
        metadata_["processing"]["processed_pairs"] = pair_count;
        metadata_["processing"]["inference_calls"] = inference_calls;
        metadata_["processing"]["inference_cache_hits"] = cache_hits;
        metadata_["processing"]["hand_inference_duration_ms"] = age(started);
        metadata_["processing"]["geometry_duration_ms"] = geometry_ms;
        metadata_["processing"]["frame_to_result_age_ms"] = {
            f[0].image.empty() ? Json(nullptr) : Json(age(f[0].received)),
            f[1].image.empty() ? Json(nullptr) : Json(age(f[1].received))};
        metadata_["processing"].erase("error");
      } catch (const std::exception& e) {
        std::lock_guard lock(mutex_);
        if (!revisions_.accepts(revision)) {
          ++discarded_work_;
          continue;
        }
        metadata_["processing"]["error"] = e.what();
        tracking_ = {};
        tracking_.reason = "processing_error";
        tracker_.reset();
        stop_interaction("processing_error");
        if (calibration_.active()) {
          auto& progress = metadata_[calibration_.stereo() ? "calibration" : "intrinsics"];
          progress["status"] = "failed";
          progress["message"] = e.what();
          calibration_.cancel();
          revisions_.calibration_changed();
        }
      }
    } else {
      std::lock_guard lock(mutex_);
      if (age(pairing_time(f[0], 0, active_phone_offset_ms_)) > 250 ||
          age(pairing_time(f[1], 1, active_phone_offset_ms_)) > 250) {
        tracking_ = {};
        tracking_.reason = "stale_frames";
        stop_interaction("stale_frames");
        tracker_.reset();
      } else if (tracker_.expire(tracking_)) {
        stop_interaction("hand_lost_in_one_or_both_views");
      }
    }

    std::optional<DiagnosticSession::Work> diagnostic_work;
    {
      std::lock_guard lock(mutex_);
      diagnostic_work = diagnostics_.work();
    }
    const auto estimate =
        diagnostic_work ? estimate_motion_delay(diagnostic_work->samples) : TimingEstimate{};
    Json event;
    TrackingResult captured;
    bool capture_ready = false;
    {
      std::lock_guard lock(mutex_);
      if (capture.enabled()) {
        captured = tracking_;
        capture_ready = true;
      }
      if (diagnostic_work && diagnostics_.finish(diagnostic_work->generation, estimate))
        enqueue_event(
            {{"event", "timing_estimate"}, {"result", diagnostics_.snapshot()["timing_estimate"]}});
      diagnostics_.expire();
      const auto frames_ = this->frames_.latest();
      const auto tracking = tracking_json(tracking_);
      const bool tracked =
          tracking["status"] == "tracked" && tracking.value("source", "missing") == "measured";
      if (was_tracking && !tracked) ++losses;
      if (!was_tracking && tracked) ++recoveries;
      was_tracking = tracked;
      const auto status = tracking["status"].dump() + tracking["reason"].dump() +
                          tracking.value("source", "missing") + interaction_.state().state;
      const double since_log = age(last_log);
      if (since_log >= (diagnostics_.active() ? 100 : 1000) ||
          (status != last_status && since_log >= 100)) {
        event = {{"event", "tracking_sample"},
                 {"timestamp_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count()},
                 {"status", tracking["status"]},
                 {"reason", tracking["reason"]},
                 {"losses", losses},
                 {"recoveries", recoveries},
                 {"confidence", tracking["confidence"]},
                 {"timing_error_ms", tracking["timing_error_ms"]},
                 {"arrival_delta_ms", tracking.value("arrival_delta_ms", Json(nullptr))},
                 {"processing", metadata_["processing"]}};
        event["processing"].update(diagnostics_.snapshot());
        event["interaction"] = interaction_json(interaction_.state());
        for (const auto* key :
             {"source", "secondary_source", "hold_age_ms", "secondary_hold_age_ms"})
          if (tracking.contains(key)) event[key] = tracking[key];
        for (int i = 0; i < 2; ++i) {
          const std::string camera = i == 0 ? "laptop" : "phone";
          const auto& points = tracking[camera + "_hands_landmarks_normalized"];
          event[camera] = {
              {"hands", points.is_array() ? points.size() : 0},
              {"frame_age_ms",
               frames_[i].image.empty() ? Json(nullptr) : Json(age(frames_[i].received))},
              {"sample_age_ms", f[i].image.empty() ? Json(nullptr) : Json(age(f[i].received))},
              {"sequence", f[i].sequence},
              {"pair_sample_age_ms",
               f[i].image.empty() ? Json(nullptr)
                                  : Json(age(pairing_time(f[i], i, active_phone_offset_ms_)))},
              {"media_pts_ms", f[i].media_pts_ms ? Json(*f[i].media_pts_ms) : Json(nullptr)},
              {"receiver_lateness_ms",
               f[i].receiver_lateness_ms ? Json(*f[i].receiver_lateness_ms) : Json(nullptr)},
              {"width", frames_[i].image.cols},
              {"height", frames_[i].image.rows}};
        }
        for (const auto* key : {"candidate_residual_px", "laptop_confidences", "phone_confidences"})
          if (tracking.contains(key)) event[key] = tracking[key];
        if (config_.tracking_filter_compare) {
          const auto comparison = filter_comparison_json(tracking_);
          if (!comparison.is_null()) event["filter_comparison"] = comparison;
        }
        last_status = status;
        last_log = Clock::now();
      }
    }
    if (!event.is_null()) log.write(event);
    // Serialization and disk I/O stay outside every runtime lock.
    if (capture_ready) capture.record(captured, processed);
    std::deque<Json> connection_events;
    {
      std::lock_guard lock(mutex_);
      connection_events.swap(transport_events_);
    }
    for (auto& connection : connection_events) {
      connection["timestamp_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
      log.write(connection);
    }
    const auto interval = std::chrono::milliseconds(1000 / config_.inference_fps);
    std::this_thread::sleep_until(started + interval);
    {
      std::lock_guard lock(mutex_);
      metadata_["processing"]["hand_inference_fps"] =
          processed ? 1000. / std::max(1., age(started)) : 0.;
    }
  }
}
void Runtime::load_calibration() {
  if (!std::filesystem::exists(config_.calibration_file)) {
    metadata_["processing"]["calibration_storage"] = "missing";
    return;
  }
  auto geometry = read_calibration(config_.calibration_file);
  intrinsics_[0] = geometry.intrinsics[0];
  intrinsics_[1] = geometry.intrinsics[1];
  stereo_ = geometry.stereo;
  const auto health = stereo_.health == CalibrationHealth::kHealthy ? "healthy" : "degraded";
  if (stereo_.calibrated)
    metadata_["calibration"].update(
        {{"status", "calibrated"},
         {"health", health},
         {"message", "Loaded saved fixed-rig calibration."},
         {"baseline_cm", stereo_.baseline_cm},
         {"median_reprojection_error_px", stereo_.median_reprojection_error_px}});
  metadata_["processing"]["calibration_loaded"] = stereo_.calibrated;
  metadata_["processing"]["calibration_storage"] = "loaded";
  const bool laptop = intrinsics_[0].valid(), phone = intrinsics_[1].valid();
  if (laptop || phone)
    metadata_["intrinsics"].update(
        {{"status", "loaded"},
         {"message", laptop && phone ? "Saved profiles loaded for both cameras."
                     : laptop ? "Saved laptop profile loaded. Calibrate the phone camera."
                              : "Saved phone profile loaded. Calibrate the laptop camera."}});
}
Json Runtime::cameras(const std::array<CameraFrame, 2>& frames_,
                      const CameraIntrinsics (&intrinsics_)[2], const StereoSolution& stereo_) {
  if (!stereo_.calibrated) return nullptr;
  Json result;
  for (int i = 0; i < 2; ++i) {
    if (frames_[i].image.empty()) return nullptr;
    const auto p = intrinsics_[i].scaled_for_active_frame(
        i == 0 ? "laptop" : "phone", frames_[i].image.cols, frames_[i].image.rows);
    if (!p) return nullptr;
    std::vector<double> distortion(8, 0);
    for (std::size_t j = 0; j < std::min<std::size_t>(8, p->distortion_coefficients.total()); ++j)
      distortion[j] = p->distortion_coefficients.at<double>(static_cast<int>(j));
    result[i == 0 ? "laptop" : "phone"] = {
        {"width", p->width},
        {"height", p->height},
        {"intrinsics",
         {p->camera_matrix(0, 0), p->camera_matrix(1, 1), p->camera_matrix(0, 2),
          p->camera_matrix(1, 2)}},
        {"distortion", distortion},
        {"rotation", matrix(i == 0 ? cv::Matx33d::eye() : stereo_.rotation_phone_from_laptop)},
        {"translation", vec(i == 0 ? cv::Vec3d{} : stereo_.translation_phone_from_laptop_m)}};
  }
  return result;
}
}  // namespace dualview
