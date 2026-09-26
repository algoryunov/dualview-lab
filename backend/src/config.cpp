#include "dualview/config.hpp"

#include <charconv>
#include <cstdlib>
#include <regex>
#include <stdexcept>

namespace dualview {
namespace {
std::string env(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return value && *value ? value : fallback;
}
bool flag(const char* name, bool fallback) {
  const auto value = env(name, fallback ? "true" : "false");
  if (value == "true") return true;
  if (value == "false") return false;
  throw std::invalid_argument(std::string(name) + " must be true or false");
}
int integer(const char* name, int fallback, int minimum, int maximum) {
  const auto text = env(name, std::to_string(fallback));
  int value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value < minimum ||
      value > maximum) {
    throw std::invalid_argument(std::string(name) + " must be an integer in [" +
                                std::to_string(minimum) + ", " + std::to_string(maximum) + "]");
  }
  return value;
}
std::string origin(bool tls, int port) {
  auto value = env("DUALVIEW_PUBLIC_ORIGIN",
                   std::string(tls ? "https" : "http") + "://localhost:" + std::to_string(port));
  const std::regex pattern(R"(^https?://(\[[a-fA-F0-9:]+\]|[a-zA-Z0-9.-]+)(:([0-9]+))?/?$)");
  std::smatch match;
  if (!std::regex_match(value, match, pattern)) {
    throw std::invalid_argument(
        "DUALVIEW_PUBLIC_ORIGIN must be an HTTP(S) origin without credentials, path, query, or "
        "fragment");
  }
  if (match[3].matched) {
    const auto text = match[3].str();
    int port_value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port_value);
    if (error != std::errc{} || end != text.data() + text.size() || port_value < 1 ||
        port_value > 65535) {
      throw std::invalid_argument("DUALVIEW_PUBLIC_ORIGIN has an invalid port");
    }
  }
  if (value.back() == '/') value.pop_back();
  return value;
}
}  // namespace
Config::Config()
    : port(integer("DUALVIEW_PORT", 8443, 1, 65535)),
      host(env("DUALVIEW_HOST", "0.0.0.0")),
      tls(flag("DUALVIEW_TLS", true)),
      public_origin(origin(tls, port)),
      show_qr(flag("DUALVIEW_SHOW_QR_CODE", true)),
      pairing_required(flag("DUALVIEW_PAIRING_REQUIRED", true)),
      camera_enabled(flag("DUALVIEW_CAMERA_ENABLED", true)),
      camera_index(integer("DUALVIEW_CAMERA_INDEX", 0, 0, 1024)),
      provider(env("DUALVIEW_INFERENCE_PROVIDER", "cpu")),
      allow_cpu_fallback(flag("DUALVIEW_INFERENCE_ALLOW_CPU_FALLBACK", false)),
      inference_enabled(flag("DUALVIEW_INFERENCE_ENABLED", true)),
      inference_fps(integer("DUALVIEW_INFERENCE_MAX_FPS", 15, 1, 60)),
      preview_bitrate(integer("DUALVIEW_PREVIEW_BITRATE", 6000000, 500000, 20000000)),
      max_pair_error_ms(integer("DUALVIEW_MAX_PAIR_ERROR_MS", 80, 40, 300)),
      phone_time_offset_ms(integer("DUALVIEW_PHONE_TIME_OFFSET_MS", 0, -300, 300)),
      tracking_log(env("DUALVIEW_TRACKING_LOG", "logs/dualview-native.jsonl")),
      metal_log(env("DUALVIEW_METAL_LOG", "logs/dualview-metal.jsonl")),
      models(env("DUALVIEW_MODELS_DIR", "models/onnx")),
      calibration_file(env("DUALVIEW_CALIBRATION_FILE", "data/calibration/native.yml")),
      frontend(env("DUALVIEW_FRONTEND_DIR", "frontend/dist")),
      certificate(env("DUALVIEW_TLS_CERT", "certs/dualview-lab.pem")),
      key(env("DUALVIEW_TLS_KEY", "certs/dualview-lab-key.pem")) {}
}  // namespace dualview
