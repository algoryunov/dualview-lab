#include "dualview/runtime.hpp"

#include <cassert>
#include <iostream>
#include <opencv2/core/persistence.hpp>

#include "dualview/runtime_protocol.hpp"
int main() {
  struct TestDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("dualview-runtime-test-" +
         std::to_string(dualview::Clock::now().time_since_epoch().count()));
    TestDirectory() { std::filesystem::create_directories(path); }
    ~TestDirectory() { std::filesystem::remove_all(path); }
  } directory;
  const std::string calibration = (directory.path / "native.yml").string();
  {
    cv::FileStorage file(calibration, cv::FileStorage::WRITE);
    file << "width0" << 640 << "height0" << 480 << "K0"
         << cv::Mat(cv::Matx33d(550, 0, 320, 0, 550, 240, 0, 0, 1)) << "D0"
         << cv::Mat::zeros(1, 5, CV_64F) << "width1" << 640 << "height1" << 480 << "K1"
         << cv::Mat(cv::Matx33d(550, 0, 320, 0, 550, 240, 0, 0, 1)) << "D1"
         << cv::Mat::zeros(1, 5, CV_64F) << "health" << "healthy" << "calibrated" << 1 << "R"
         << cv::Mat(cv::Matx33d::eye()) << "T" << cv::Mat(cv::Vec3d(-.12, 0, 0)) << "baseline_cm"
         << 12.0 << "error_px" << .5;
  }
  dualview::Config loaded;
  loaded.camera_enabled = false;
  loaded.inference_enabled = false;
  loaded.tracking_log = "-";
  loaded.metal_log = (directory.path / "metal.jsonl").string();
  loaded.calibration_file = calibration;
  dualview::Runtime restored(loaded);
  assert(restored.snapshot()["calibration"]["status"] == "calibrated");
  assert(restored.snapshot()["processing"]["calibration_loaded"] == true);
  auto saved = restored.snapshot();
  assert(saved["intrinsics"]["status"] == "loaded");
  assert(saved["intrinsics"]["profiles"]["laptop"]["available"] == true);
  assert(saved["intrinsics"]["profiles"]["phone"]["compatibility"] == "waiting_for_camera");
  dualview::Json sample = {{"mode", "hands"},
                           {"phase", "two_hands"},
                           {"camera", "laptop"},
                           {"client_ms", 1234},
                           {"dt_ms", 16},
                           {"hands", 2},
                           {"distance_m", .2},
                           {"separation", 1.3},
                           {"neck", 1},
                           {"opacity", 1},
                           {"center_m", {0, 0, .5}},
                           {"axis", {1, 0, 0}},
                           {"blobs", std::vector<double>(20, 0)},
                           {"velocities", std::vector<double>(15, 0)}};
  dualview::dispatch_command(restored, {{"command", "metal.sample"}, {"sample", sample}});
  std::ifstream metal_input(loaded.metal_log);
  dualview::Json event;
  metal_input >> event;
  assert(event["event"] == "metal_sample");
  assert(event["center_m"] == sample["center_m"]);
  assert(event.contains("interaction") && event.contains("timestamp_ms"));
  assert(restored.snapshot()["processing"]["metal_log_samples"] == 1);
  assert(restored.snapshot()["processing"]["metal_log_file"] == loaded.metal_log);
  sample["blobs"] = {0};
  bool invalid_sample = false;
  try {
    dualview::dispatch_command(restored, {{"command", "metal.sample"}, {"sample", sample}});
  } catch (const std::invalid_argument&) {
    invalid_sample = true;
  }
  assert(invalid_sample);
  restored.receive_phone(cv::Mat::zeros(960, 1280, CV_8UC3));
  assert(restored.snapshot()["intrinsics"]["profiles"]["phone"]["compatibility"] == "compatible");
  restored.receive_phone(cv::Mat::zeros(720, 1280, CV_8UC3));
  assert(restored.snapshot()["intrinsics"]["profiles"]["phone"]["compatibility"] ==
         "mode_mismatch");
  restored.clear_phone();
  assert(restored.snapshot()["intrinsics"]["profiles"]["phone"]["available"] == true);
  // Intrinsic profiles must still be restored if no stereo solution was saved.
  const auto intrinsic_file = (directory.path / "intrinsics-only.yml").string();
  {
    cv::FileStorage file(intrinsic_file, cv::FileStorage::WRITE);
    file << "width0" << 640 << "height0" << 480 << "K0"
         << cv::Mat(cv::Matx33d(550, 0, 320, 0, 550, 240, 0, 0, 1)) << "D0"
         << cv::Mat::zeros(1, 5, CV_64F) << "calibrated" << 0;
  }
  loaded.calibration_file = intrinsic_file;
  dualview::Runtime intrinsic_only(loaded);
  assert(intrinsic_only.snapshot()["intrinsics"]["status"] == "loaded");
  assert(intrinsic_only.snapshot()["intrinsics"]["profiles"]["laptop"]["available"] == true);
  assert(intrinsic_only.snapshot()["intrinsics"]["profiles"]["phone"]["available"] == false);
  assert(intrinsic_only.snapshot()["processing"]["calibration_loaded"] == false);
  dualview::Config config;
  config.tracking_log = "-";
  config.camera_enabled = false;
  config.inference_enabled = false;
  config.calibration_file = (directory.path / "missing.yml").string();
  config.phone_time_offset_ms = 17;
  dualview::Runtime runtime(config);
  assert(runtime.snapshot()["processing"]["phone_time_offset_ms"] == 17);
  assert(dualview::dispatch_command(runtime, {{"command", "timing.reset"}})["offset_ms"] == 0);
  assert(runtime.snapshot()["processing"]["phone_time_offset_ms"] == 0);
  runtime.clear_phone();
  assert(runtime.snapshot()["processing"]["phone_time_offset_ms"] == 17);
  bool no_estimate = false;
  try {
    dualview::dispatch_command(runtime, {{"command", "timing.apply"}});
  } catch (const std::invalid_argument&) {
    no_estimate = true;
  }
  assert(no_estimate);
  runtime.transport_diagnostics(
      {{"jitter_ms", 12.0}, {"round_trip_ms", -1}, {"private", "ignored"}});
  runtime.transport_event("user_stop");
  auto state = runtime.snapshot();
  assert(state["processing"]["phone_transport"]["jitter_ms"] == 12.0);
  assert(!state["processing"]["phone_transport"].contains("round_trip_ms"));
  assert(!state["processing"]["phone_transport"].contains("private"));
  assert(state["processing"]["phone_connection_event"] == "user_stop");
  bool phase_rejected = false;
  try {
    dualview::dispatch_command(runtime,
                               {{"command", "diagnostics.phase"}, {"phase", "stationary"}});
  } catch (const std::invalid_argument&) {
    phase_rejected = true;
  }
  assert(phase_rejected);
  assert(dualview::dispatch_command(
             runtime, {{"command", "diagnostics.phase"}, {"phase", "idle"}})["phase"] == "idle");
  assert(!state["phone"]["available"].get<bool>());
  assert(state["cameras"].is_null());
  bool rejected = false;
  try {
    dualview::dispatch_command(runtime, {{"command", "intrinsics.start"}, {"camera", "laptop"}});
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
  rejected = false;
  try {
    dualview::dispatch_command(runtime,
                               {{"command", "interaction.model"}, {"model_id", "unknown"}});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);
  dualview::dispatch_command(runtime, {{"command", "interaction.model"}, {"model_id", "flower"}});
  assert(runtime.snapshot()["interaction"]["model_id"] == "flower");
  runtime.receive_phone(cv::Mat(360, 640, CV_8UC3, cv::Scalar(0, 0, 0)));
  state = runtime.snapshot();
  assert(state["phone"]["available"].get<bool>());
  assert(state["phone"]["width"] == 640);
  dualview::dispatch_command(runtime, {{"command", "intrinsics.start"}, {"camera", "phone"}});
  assert(runtime.snapshot()["intrinsics"]["status"] == "capturing");
  assert(runtime.snapshot()["intrinsics"]["required_views"] == 25);
  assert(runtime.snapshot()["intrinsics"]["elapsed_ms"].get<double>() >= 0);
  rejected = false;
  try {
    dualview::dispatch_command(runtime, {{"command", "calibration.start"}});
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
  dualview::dispatch_command(runtime, {{"command", "calibration.cancel"}});
  dualview::dispatch_command(runtime, {{"command", "intrinsics.start"}, {"camera", "phone"}});
  runtime.clear_phone();
  assert(runtime.snapshot()["intrinsics"]["status"] == "not_started");
  runtime.clear_phone();
  state = runtime.snapshot();
  assert(!state["phone"]["available"].get<bool>());
  assert(state["phone_state"] == "disconnected");
  assert(state["interaction"]["interaction_block_reason"] == "phone_disconnected" ||
         state["interaction"]["interaction_block_reason"] == "stale_frames");
  assert(state["interaction"]["cursor_m"].is_null());
  assert(!state["interaction"]["pinch"].get<bool>());
  dualview::dispatch_command(runtime, {{"command", "interaction.reset"}});
  assert(runtime.snapshot()["interaction"]["model_id"] == "flower");
  std::cout << "Runtime command validation, capture lifecycle, and disconnect verified\n";
}
