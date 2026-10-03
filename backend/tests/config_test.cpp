#include "dualview/config.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

bool rejects(const char* key, const char* value) {
  setenv(key, value, 1);
  bool rejected = false;
  try {
    dualview::Config config;
  } catch (const std::invalid_argument& error) {
    rejected = std::string(error.what()).find(key) != std::string::npos;
  }
  unsetenv(key);
  return rejected;
}
int main() {
  assert(rejects("DUALVIEW_PHONE_TIME_OFFSET_MS", "301"));
  assert(dualview::Config{}.phone_time_offset_ms == 0);
  assert(rejects("DUALVIEW_PREVIEW_BITRATE", "0"));
  assert(rejects("DUALVIEW_PREVIEW_BITRATE", "20000001"));
  assert(rejects("DUALVIEW_MAX_PAIR_ERROR_MS", "20"));
  assert(dualview::Config{}.max_pair_error_ms == 80);
  assert(dualview::Config{}.preview_bitrate == 6000000);
  assert(rejects("DUALVIEW_PORT", "8443garbage"));
  assert(rejects("DUALVIEW_PORT", "65536"));
  assert(rejects("DUALVIEW_INFERENCE_MAX_FPS", "0"));
  assert(rejects("DUALVIEW_INFERENCE_MAX_FPS", "15.5"));
  assert(rejects("DUALVIEW_SHOW_QR_CODE", "yes"));
  assert(rejects("DUALVIEW_PUBLIC_ORIGIN", "https://host/path"));
  assert(rejects("DUALVIEW_PUBLIC_ORIGIN", "https://user:password@host"));
  assert(rejects("DUALVIEW_PUBLIC_ORIGIN", "https://host:99999"));
  setenv("DUALVIEW_PUBLIC_ORIGIN", "https://host:8443/", 1);
  assert(dualview::Config{}.public_origin == "https://host:8443");
  unsetenv("DUALVIEW_PUBLIC_ORIGIN");
  setenv("DUALVIEW_PORT", "18445", 1);
  setenv("DUALVIEW_TLS", "false", 1);
  assert(dualview::Config{}.public_origin == "http://localhost:18445");
  unsetenv("DUALVIEW_PORT");
  unsetenv("DUALVIEW_TLS");

  // Tracking-filter selection defaults to the supported estimator, and an
  // unrecognised name must fail loudly rather than quietly changing estimator.
  assert(dualview::Config{}.tracking_filter == dualview::FilterStrategy::AlphaOnly);
  assert(!dualview::Config{}.tracking_filter_compare);
  assert(rejects("DUALVIEW_TRACKING_FILTER", "ekf"));
  assert(rejects("DUALVIEW_TRACKING_FILTER", "Kalman"));
  assert(rejects("DUALVIEW_TRACKING_FILTER_COMPARE", "1"));
  assert(rejects("DUALVIEW_TRACKING_KALMAN_ACCEL", "0"));
  assert(rejects("DUALVIEW_TRACKING_KALMAN_ACCEL", "4x"));
  assert(rejects("DUALVIEW_TRACKING_FILTER_BETA", "1.5"));
  setenv("DUALVIEW_TRACKING_FILTER", "kalman", 1);
  setenv("DUALVIEW_TRACKING_FILTER_COMPARE", "true", 1);
  setenv("DUALVIEW_TRACKING_KALMAN_ACCEL", "2.5", 1);
  {
    const dualview::Config config;
    assert(config.tracking_filter == dualview::FilterStrategy::Kalman);
    assert(config.tracking_filter_compare);
    assert(config.filter_tuning.kalman_accel_sigma == 2.5);
    assert(config.filter_tuning.beta == .35);
  }
  unsetenv("DUALVIEW_TRACKING_FILTER");
  unsetenv("DUALVIEW_TRACKING_FILTER_COMPARE");
  unsetenv("DUALVIEW_TRACKING_KALMAN_ACCEL");

  std::cout << "Strict environment parsing and origin normalization verified\n";
}
