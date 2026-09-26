#pragma once
#include <atomic>
#include <mutex>

#include "dualview/event_log.hpp"
#include "dualview/interaction.hpp"
namespace dualview {
// Its lock is never acquired while holding Runtime's state lock.
class MetalDiagnostics {
 public:
  explicit MetalDiagnostics(const std::string& path)
      : log_(path),
        path_(path == "-" ? "stderr"
                          : std::filesystem::absolute(path).lexically_normal().string()) {}
  void record(nlohmann::json sample, const TrackingResult& tracking,
              const InteractionState& interaction) {
    std::lock_guard lock(mutex_);
    const auto now = Clock::now();
    if (now - last_ < std::chrono::milliseconds(90)) return;
    sample["event"] = "metal_sample";
    sample["timestamp_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    const auto h = tracking_json(tracking);
    sample["tracking"] = {{"status", h["status"]},
                          {"reason", h["reason"]},
                          {"source", h["source"]},
                          {"secondary_source", h["secondary_source"]},
                          {"timing_error_ms", h["timing_error_ms"]}};
    sample["interaction"] = interaction_json(interaction);
    failed_ = !log_.write(sample);
    if (failed_)
      throw std::runtime_error("Could not write metal diagnostics; check the server log path");
    ++count_;
    last_ = now;
  }
  nlohmann::json status() const {
    nlohmann::json result = {{"metal_log_file", path_}, {"metal_log_samples", count_.load()}};
    if (failed_) result["metal_log_error"] = "Could not write metal diagnostics";
    return result;
  }

 private:
  mutable std::mutex mutex_;
  EventLog log_;
  const std::string path_;
  Clock::time_point last_{};
  std::atomic<std::uint64_t> count_{0};
  std::atomic<bool> failed_{false};
};
}  // namespace dualview
