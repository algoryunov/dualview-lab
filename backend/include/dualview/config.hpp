#pragma once
#include <string>

#include "dualview/landmark_filter.hpp"

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
  // The estimator that produces the published pose. "alpha" is the supported
  // path; the others are experimental and not validated on physical hardware.
  FilterStrategy tracking_filter;
  // Run the remaining estimators alongside the published one and report their
  // divergence. Costs extra work per frame and changes no interaction output.
  bool tracking_filter_compare;
  FilterTuning filter_tuning;
  // Per-frame landmark recording for offline estimator comparison. Empty
  // disables it; recording stops after tracking_capture_seconds.
  std::string tracking_capture;
  int tracking_capture_seconds;
  std::string tracking_log;
  std::string metal_log;
  std::string models;
  std::string calibration_file;
  std::string frontend;
  std::string certificate;
  std::string key;
};
}  // namespace dualview
