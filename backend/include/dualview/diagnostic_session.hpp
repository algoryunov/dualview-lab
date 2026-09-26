#pragma once
#include <map>

#include "dualview/hand_pipeline.hpp"
#include "dualview/motion_timing.hpp"
#include "dualview/runtime_command.hpp"
#include "dualview/synchronized_frames.hpp"
namespace dualview {
// No internal lock: owner serializes transitions. Work is copied out for correlation.
class DiagnosticSession {
 public:
  struct Work {
    std::uint64_t generation;
    std::deque<MotionSample> samples[2];
  };
  void start(DiagnosticPhase phase, Clock::time_point now = Clock::now());
  void reset();
  bool active(Clock::time_point now = Clock::now()) const { return now < until_; }
  std::uint64_t generation() const { return generation_; }
  void observe(const std::string& reason, Clock::time_point now = Clock::now()) {
    if (active(now)) ++counts_[reason];
  }
  void collect(std::uint64_t generation, const CameraFrame (&frames)[2],
               const std::vector<Hand> (&hands)[2], Clock::time_point now = Clock::now());
  std::optional<Work> work(Clock::time_point now = Clock::now()) const;
  bool finish(std::uint64_t generation, const TimingEstimate& estimate,
              Clock::time_point now = Clock::now());
  void expire(Clock::time_point now = Clock::now()) {
    if (!active(now) && phase_ != DiagnosticPhase::Timing) phase_ = DiagnosticPhase::Idle;
  }
  int suggested_offset(Clock::time_point now = Clock::now()) const;
  nlohmann::json snapshot() const;

 private:
  enum class EstimateStatus { Missing, Collecting, Cancelled, Ready, Unreliable };
  DiagnosticPhase phase_ = DiagnosticPhase::Idle;
  EstimateStatus status_ = EstimateStatus::Missing;
  std::uint64_t generation_ = 0, sequences_[2]{};
  Clock::time_point until_{}, estimated_at_{};
  std::deque<MotionSample> samples_[2];
  TimingEstimate estimate_;
  std::map<std::string, std::uint64_t> counts_;
};
}  // namespace dualview
