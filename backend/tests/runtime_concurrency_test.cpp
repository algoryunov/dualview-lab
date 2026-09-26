#include <barrier>
#include <cassert>
#include <thread>

#include "dualview/runtime.hpp"
#include "dualview/runtime_protocol.hpp"
using namespace dualview;
int main() {
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("dualview-concurrency-" + std::to_string(Clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  Config config;
  config.camera_enabled = config.inference_enabled = false;
  config.calibration_file = (directory / "calibration.yml").string();
  config.tracking_log = (directory / "tracking.jsonl").string();
  config.metal_log = (directory / "metal.jsonl").string();
  {
    Runtime runtime(config);
    std::barrier start(4);
    std::jthread input([&] {
      start.arrive_and_wait();
      for (int i = 0; i < 250; ++i) runtime.receive_phone(cv::Mat::zeros(48, 64, CV_8UC3));
    });
    std::jthread commands([&] {
      start.arrive_and_wait();
      for (int i = 0; i < 100; ++i) {
        runtime.execute(SelectModel{i % 2 ? "car" : "flower"});
        runtime.execute(ResetInteraction{});
        runtime.execute(ResetTiming{});
        runtime.clear_phone();
      }
    });
    std::jthread snapshots([&] {
      start.arrive_and_wait();
      for (int i = 0; i < 250; ++i) {
        const auto snapshot = runtime.snapshot();
        assert(snapshot["hand_tracking"]["status"] == "unavailable");
        assert(snapshot["interaction"]["pinch"] == false);
        assert(snapshot["interaction"]["object_scale_m"] == .11);
        assert(snapshot["processing"]["frame_input"]["history_depth"][1].get<int>() <= 8);
      }
    });
    start.arrive_and_wait();
    input.join();
    commands.join();
    snapshots.join();
    for (int i = 0; i < 30; ++i) {
      runtime.receive_phone(cv::Mat::zeros(48, 64, CV_8UC3));
      runtime.execute(StartIntrinsics{CameraId::Phone});
      runtime.execute(CancelCalibration{});
      assert(runtime.snapshot()["intrinsics"]["status"] == "not_started");
    }
    runtime.clear_phone();
    assert(runtime.frame(CameraId::Phone).image.empty());
    assert(!std::filesystem::exists(config.calibration_file));
  }
  std::filesystem::remove_all(directory);
}
