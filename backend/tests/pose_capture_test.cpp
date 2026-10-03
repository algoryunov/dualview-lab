#include "dualview/pose_capture.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
using namespace dualview;
namespace {

std::vector<nlohmann::json> read_lines(const std::filesystem::path& path) {
  std::vector<nlohmann::json> records;
  std::ifstream input(path);
  for (std::string line; std::getline(input, line);)
    if (!line.empty()) records.push_back(nlohmann::json::parse(line));
  return records;
}

TrackingResult sample_result(bool with_alternate) {
  TrackingResult result;
  auto& hand = result.hands[0];
  for (int j = 0; j < 21; ++j) {
    hand.raw.push_back({.001 * j, .002 * j, .6 + .00001234 * j});
    hand.filtered.push_back({.001 * j, .002 * j, .6});
    hand.residuals.push_back(1.5);
  }
  hand.confidence = .9;
  hand.source = PoseSource::Measured;
  result.candidate_residual_px = 4.25;
  result.timing_error_ms = 7.5;
  if (with_alternate) hand.alternates.push_back({"kalman", hand.raw, 2.5});
  return result;
}

}  // namespace

int main() {
  const auto directory = std::filesystem::temp_directory_path() / "dualview-pose-capture-test";
  std::filesystem::remove_all(directory);
  const auto path = directory / "capture.jsonl";

  // An empty path disables recording entirely: no file, no work.
  {
    PoseCapture capture("", 120);
    assert(!capture.enabled());
    capture.begin({{"tracking_filter", "alpha"}});
    capture.record(sample_result(true), true);
    assert(capture.samples() == 0);
  }

  // A configured capture records every processed pair, with landmarks.
  {
    PoseCapture capture(path.string(), 120);
    assert(capture.enabled());
    capture.begin({{"tracking_filter", "alpha"}, {"baseline_cm", 12.0}});
    for (int i = 0; i < 5; ++i) capture.record(sample_result(true), true);
    // Frames where no new pair was processed must not create duplicate rows.
    capture.record(sample_result(true), false);
    assert(capture.samples() == 5);

    const auto records = read_lines(path);
    assert(records.size() == 6);  // one header plus five samples
    assert(records[0]["event"] == "capture_header");
    assert(records[0]["schema"] == "dualview.pose_capture.v1");
    assert(records[0]["tracking_filter"] == "alpha");
    assert(records[0]["limit_seconds"] == 120);

    const auto& first = records[1];
    assert(first["event"] == "pose_sample");
    assert(first["candidate_residual_px"].get<double>() == 4.25);
    const auto& primary = first["primary"];
    assert(primary["source"] == "measured");
    assert(primary["raw_points_m"].size() == 21);
    assert(primary["filtered_points_m"].size() == 21);
    assert(primary["reprojection_residuals_px"].size() == 21);
    assert(primary["filter_alternates"].size() == 1);
    assert(primary["filter_alternates"][0]["strategy"] == "kalman");
    assert(primary["filter_alternates"][0]["filtered_points_m"].size() == 21);
    // Landmarks keep 10 micrometre resolution.
    const double z = primary["raw_points_m"][1][2].get<double>();
    assert(std::abs(z - .60001) < 1e-9);
    assert(first["secondary"].is_null());
    assert(records[1]["t_s"].get<double>() >= 0);
  }

  // Recording stops on its own once the duration limit passes, and says so.
  {
    const auto bounded = directory / "bounded.jsonl";
    PoseCapture capture(bounded.string(), 5);
    capture.begin({{"tracking_filter", "alpha"}});
    capture.record(sample_result(false), true);
    assert(capture.samples() == 1);
    // Re-run the limit check against an elapsed time beyond the budget.
    PoseCapture expired(bounded.string(), 0);
    expired.begin({{"tracking_filter", "alpha"}});
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    expired.record(sample_result(false), true);
    assert(expired.samples() == 0);
    assert(!expired.enabled());  // Finished captures stay closed.
    expired.record(sample_result(false), true);
    assert(expired.samples() == 0);

    const auto records = read_lines(bounded);
    bool completed = false;
    for (const auto& record : records)
      if (record["event"] == "capture_complete") {
        completed = true;
        assert(record["reason"] == "duration_limit");
      }
    assert(completed);
  }

  std::filesystem::remove_all(directory);
  std::printf("Per-frame pose capture records landmarks and honours its bound\n");
  return 0;
}
