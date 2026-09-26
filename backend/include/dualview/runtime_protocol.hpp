#pragma once
#include "dualview/runtime_command.hpp"
namespace dualview {
class Runtime;
RuntimeCommand parse_command(const nlohmann::json& request);
nlohmann::json dispatch_command(Runtime& runtime, const nlohmann::json& request);
}  // namespace dualview
