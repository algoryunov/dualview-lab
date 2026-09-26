#include "dualview/runtime_protocol.hpp"

#include <cmath>

#include "dualview/runtime.hpp"
namespace dualview {
using Json = nlohmann::json;
namespace {
MetalSample parse_metal(const Json& sample) {
  if (!sample.is_object() || sample.dump().size() > 4096)
    throw std::invalid_argument("Invalid metal sample");
  Json clean;
  const auto renderer = sample.value("renderer", "legacy-blobs");
  if (renderer.size() > 32) throw std::invalid_argument("Invalid renderer label");
  clean["renderer"] = renderer;
  for (const auto* key : {"mode", "phase", "camera"}) {
    const auto value = sample.at(key).get<std::string>();
    if (value.size() > 32) throw std::invalid_argument("Invalid metal sample label");
    clean[key] = value;
  }
  for (const auto* key :
       {"client_ms", "dt_ms", "hands", "distance_m", "separation", "neck", "opacity"}) {
    const double value = sample.at(key).get<double>();
    if (!std::isfinite(value) || std::abs(value) > 1e12)
      throw std::invalid_argument("Invalid metal sample number");
    clean[key] = value;
  }
  for (const auto* key : {"center_m", "axis", "blobs", "velocities"}) {
    const auto& values = sample.at(key);
    const std::size_t size = std::string(key) == "blobs"        ? 20
                             : std::string(key) == "velocities" ? 15
                                                                : 3;
    if (!values.is_array() || values.size() != size)
      throw std::invalid_argument("Invalid metal sample vector");
    for (const auto& value : values)
      if (!value.is_number() || !std::isfinite(value.get<double>()) ||
          std::abs(value.get<double>()) > 1e6)
        throw std::invalid_argument("Invalid metal sample coordinate");
    clean[key] = values;
  }
  return {std::move(clean)};
}
}  // namespace
const char* phase_name(DiagnosticPhase phase) {
  switch (phase) {
    case DiagnosticPhase::Idle:
      return "idle";
    case DiagnosticPhase::Stationary:
      return "stationary";
    case DiagnosticPhase::Moving:
      return "moving";
    case DiagnosticPhase::Timing:
      return "timing";
  }
  throw std::invalid_argument("Unknown diagnostic phase");
}
RuntimeCommand parse_command(const Json& request) {
  const auto name = request.at("command").get<std::string>();
  if (name == "diagnostics.phase") {
    const auto phase = request.value("phase", "idle");
    for (const auto candidate : {DiagnosticPhase::Idle, DiagnosticPhase::Stationary,
                                 DiagnosticPhase::Moving, DiagnosticPhase::Timing})
      if (phase == phase_name(candidate)) return SetDiagnosticPhase{candidate};
    throw std::invalid_argument("Unknown diagnostic phase");
  }
  if (name == "timing.apply") return ApplyTiming{};
  if (name == "timing.reset") return ResetTiming{};
  if (name == "calibration.start") return StartStereo{};
  if (name == "calibration.cancel") return CancelCalibration{};
  if (name == "interaction.reset") return ResetInteraction{};
  if (name == "interaction.model") return SelectModel{request.at("model_id").get<std::string>()};
  if (name == "metal.sample") return parse_metal(request.at("sample"));
  if (name == "intrinsics.start") {
    const auto camera = request.at("camera").get<std::string>();
    if (camera == "laptop") return StartIntrinsics{CameraId::Laptop};
    if (camera == "phone") return StartIntrinsics{CameraId::Phone};
    throw std::invalid_argument("Unknown camera");
  }
  throw std::invalid_argument("Unknown command: " + name);
}
Json dispatch_command(Runtime& runtime, const Json& request) {
  const auto result = runtime.execute(parse_command(request));
  if (result.phase) return {{"phase", phase_name(*result.phase)}};
  if (result.offset_ms) return {{"offset_ms", *result.offset_ms}};
  return {{"ok", true}};
}
}  // namespace dualview
