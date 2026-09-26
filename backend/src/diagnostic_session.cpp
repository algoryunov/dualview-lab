#include "dualview/diagnostic_session.hpp"
namespace dualview {
void DiagnosticSession::start(DiagnosticPhase phase, Clock::time_point now) {
  ++generation_;
  phase_ = phase;
  until_ = now + std::chrono::seconds(phase == DiagnosticPhase::Idle ? 0 : 20);
  if (phase == DiagnosticPhase::Timing) {
    for (auto& samples : samples_) samples.clear();
    sequences_[0] = sequences_[1] = 0;
    status_ = EstimateStatus::Collecting;
  } else if (status_ == EstimateStatus::Collecting)
    status_ = EstimateStatus::Cancelled;
  if (phase != DiagnosticPhase::Idle) counts_.clear();
}
void DiagnosticSession::reset() {
  const auto next = generation_ + 1;
  *this = DiagnosticSession{};
  generation_ = next;
}
void DiagnosticSession::collect(std::uint64_t generation, const CameraFrame (&frames)[2],
                                const std::vector<Hand> (&hands)[2], Clock::time_point now) {
  if (generation != generation_ || phase_ != DiagnosticPhase::Timing || !active(now)) return;
  for (int i = 0; i < 2; ++i) {
    const auto& f = frames[i];
    if (f.sequence <= sequences_[i]) continue;
    sequences_[i] = f.sequence;
    if (f.image.empty() || now - f.received > std::chrono::milliseconds(250) ||
        hands[i].size() != 1 || hands[i][0].confidence < .7)
      continue;
    double x = 0;
    for (int joint : {0, 5, 9, 13, 17}) x += hands[i][0].landmarks[joint].x;
    samples_[i].push_back(
        {std::chrono::duration<double, std::milli>(f.received.time_since_epoch()).count(),
         x / (5 * f.image.cols)});
    while (samples_[i].size() > 512) samples_[i].pop_front();
  }
}
std::optional<DiagnosticSession::Work> DiagnosticSession::work(Clock::time_point now) const {
  if (active(now) || phase_ != DiagnosticPhase::Timing) return {};
  return Work{generation_, {samples_[0], samples_[1]}};
}
bool DiagnosticSession::finish(std::uint64_t generation, const TimingEstimate& estimate,
                               Clock::time_point now) {
  if (generation != generation_ || active(now) || phase_ != DiagnosticPhase::Timing) return false;
  estimate_ = estimate;
  estimated_at_ = now;
  phase_ = DiagnosticPhase::Idle;
  status_ = estimate.accepted ? EstimateStatus::Ready : EstimateStatus::Unreliable;
  return true;
}
int DiagnosticSession::suggested_offset(Clock::time_point now) const {
  if (status_ != EstimateStatus::Ready || now - estimated_at_ > std::chrono::minutes(5))
    throw std::invalid_argument("No reliable timing estimate");
  return estimate_.offset_ms;
}
nlohmann::json DiagnosticSession::snapshot() const {
  using Json = nlohmann::json;
  Json result = {{"diagnostic_phase", phase_name(phase_)}, {"diagnostic_counts", counts_}};
  if (status_ == EstimateStatus::Missing) return result;
  auto& e = result["timing_estimate"];
  if (status_ == EstimateStatus::Collecting)
    e = {{"status", "collecting"}};
  else if (status_ == EstimateStatus::Cancelled)
    e = {{"status", "cancelled"}};
  else
    e = {{"status", status_ == EstimateStatus::Ready ? "ready" : "unreliable"},
         {"offset_ms", estimate_.offset_ms},
         {"correlation", estimate_.correlation},
         {"baseline_correlation", estimate_.baseline_correlation},
         {"reason", estimate_.reason}};
  return result;
}
}  // namespace dualview
