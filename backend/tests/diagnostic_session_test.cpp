#include "dualview/diagnostic_session.hpp"

#include <cassert>
using namespace dualview;
using namespace std::chrono_literals;
int main() {
  const auto now = Clock::now();
  DiagnosticSession session;
  session.start(DiagnosticPhase::Timing, now);
  assert(!session.work(now + 19s));
  auto work = session.work(now + 20s);
  assert(work);
  TimingEstimate estimate;
  estimate.accepted = true;
  estimate.offset_ms = 70;
  session.start(DiagnosticPhase::Timing, now + 20s);
  assert(!session.finish(work->generation, estimate, now + 40s));  // cancelled/replaced computation
  assert(session.snapshot()["timing_estimate"]["status"] == "collecting");
  work = session.work(now + 40s);
  assert(work);
  assert(session.finish(work->generation, estimate, now + 40s));
  assert(session.suggested_offset(now + 41s) == 70);
  assert(!session.finish(work->generation, estimate, now + 41s));  // duplicate completion
  bool rejected = false;
  try {
    session.suggested_offset(now + 341s);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);
  session.start(DiagnosticPhase::Timing, now);
  const auto generation = session.generation();
  session.start(DiagnosticPhase::Timing, now);
  CameraFrame frames[2];
  std::vector<Hand> hands[2];
  for (int i = 0; i < 2; ++i) {
    frames[i] = {cv::Mat::zeros(2, 2, CV_8UC3), now, 1, {}, {}};
    hands[i].push_back({});
    hands[i][0].confidence = .9;
  }
  session.collect(generation, frames, hands, now);
  assert(session.work(now + 20s)->samples[0].empty());
  session.collect(session.generation(), frames, hands, now);
  assert(session.work(now + 20s)->samples[0].size() == 1);
  session.collect(session.generation(), frames, hands, now);
  assert(session.work(now + 20s)->samples[0].size() ==
         1);  // cached detections aren't extra samples
  session.start(DiagnosticPhase::Idle, now);
  assert(session.snapshot()["timing_estimate"]["status"] == "cancelled");
  session.reset();
  assert(!session.snapshot().contains("timing_estimate"));
  assert(!session.finish(generation, estimate, now + 40s));
}
