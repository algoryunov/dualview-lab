#pragma once
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

#include "dualview/calibration_session.hpp"
#include "dualview/config.hpp"
#include "dualview/diagnostic_session.hpp"
#include "dualview/hand_pipeline.hpp"
#include "dualview/interaction.hpp"
#include "dualview/metal_diagnostics.hpp"
#include "dualview/runtime_command.hpp"
#include "dualview/synchronized_frames.hpp"
#include "dualview/work_revisions.hpp"
namespace dualview {
using Json = nlohmann::json;
class Runtime {
 public:
  explicit Runtime(const Config& config);
  ~Runtime();
  void receive_phone(const cv::Mat& image, std::optional<double> pts_ms = {},
                     std::optional<double> receiver_lateness_ms = {});
  void transport_diagnostics(const Json& values);
  void transport_event(const std::string& reason);
  void clear_phone();
  CameraFrame frame(CameraId camera) const;
  Json snapshot() const;
  CommandReply execute(const RuntimeCommand& command);

 private:
  // Lock contract: persistence_mutex_ -> mutex_ -> FrameStore's internal lock.
  // Never hold mutex_ during model/geometry computation, disk I/O, or metal_.record().
  // All fields below except immutable config, FrameStore, MetalDiagnostics, and worker-only
  // inference objects are protected by mutex_. WorkRevisions is checked under mutex_
  // and that lock remains held through publication. See docs/runtime-ownership.md.
  const Config config_;
  int active_phone_offset_ms_;
  mutable std::mutex mutex_;
  FrameStore frames_;
  std::mutex persistence_mutex_;
  WorkRevisions revisions_;
  std::uint64_t discarded_work_ = 0;
  std::deque<Json> transport_events_;
  // Presentation metadata only. Tracking and interaction have exactly one typed owner.
  Json metadata_;
  MetalDiagnostics metal_;
  DiagnosticSession diagnostics_;
  HandTracker tracker_;
  TrackingResult tracking_;
  InteractionController interaction_;
  // Constructed before threads; accessed only by processing_ until both threads join.
  std::unique_ptr<inference::Controller> inference_;
  std::unique_ptr<HandPipeline> hands_;
  std::jthread capture_, processing_;
  CameraIntrinsics intrinsics_[2];
  StereoSolution stereo_;
  CalibrationSession calibration_;
  void enqueue_event(Json event);
  void process(std::stop_token stop);
  void stop_interaction(const std::string& reason);
  void load_calibration();
  static Json cameras(const std::array<CameraFrame, 2>& frames,
                      const CameraIntrinsics (&intrinsics)[2], const StereoSolution& stereo);
};
}  // namespace dualview
