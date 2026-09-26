#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
namespace dualview {
enum class CameraId { Laptop, Phone };
enum class DiagnosticPhase { Idle, Stationary, Moving, Timing };
const char* phase_name(DiagnosticPhase phase);
struct SetDiagnosticPhase {
  DiagnosticPhase phase;
};
struct ApplyTiming {};
struct ResetTiming {};
struct StartIntrinsics {
  CameraId camera;
};
struct StartStereo {};
struct CancelCalibration {};
struct SelectModel {
  std::string id;
};
struct ResetInteraction {};
// Sanitized diagnostic payload, never consumed by tracking/interaction algorithms.
struct MetalSample {
  nlohmann::json values;
};
using RuntimeCommand =
    std::variant<SetDiagnosticPhase, ApplyTiming, ResetTiming, StartIntrinsics, StartStereo,
                 CancelCalibration, SelectModel, ResetInteraction, MetalSample>;
struct CommandReply {
  std::optional<DiagnosticPhase> phase;
  std::optional<int> offset_ms;
};
}  // namespace dualview
