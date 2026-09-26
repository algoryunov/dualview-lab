#pragma once
#include <string>

namespace dualview {
struct Config {
  Config();
  int port;
  std::string host;
  bool tls;
  std::string public_origin;
  bool show_qr;
  bool pairing_required;
  bool camera_enabled;
  int camera_index;
  std::string provider;
  bool allow_cpu_fallback;
  bool inference_enabled;
  int inference_fps;
  int preview_bitrate;
  int max_pair_error_ms;
  int phone_time_offset_ms;
  std::string tracking_log;
  std::string metal_log;
  std::string models;
  std::string calibration_file;
  std::string frontend;
  std::string certificate;
  std::string key;
};
}  // namespace dualview
