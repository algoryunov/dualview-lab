#include "dualview/work_revisions.hpp"

#include <cassert>
#include <latch>
#include <mutex>
#include <thread>
using namespace dualview;
int main() {
  for (int change = 0; change < 3; ++change) {
    WorkRevisions revisions;
    std::mutex mutex;
    std::latch captured(1), release(1);
    bool published = false;
    std::jthread worker([&] {
      WorkRevisions::Token token;
      {
        std::lock_guard lock(mutex);
        token = revisions.capture();
      }
      captured.count_down();
      release.wait();  // Deliberately complete old work after a transition.
      {
        std::lock_guard lock(mutex);
        if (revisions.accepts(token)) published = true;
      }
    });
    captured.wait();
    {
      std::lock_guard lock(mutex);
      if (change == 0)
        revisions.phone_replaced();
      else if (change == 1)
        revisions.timing_changed();
      else
        revisions.calibration_changed();
    }
    release.count_down();
    worker.join();
    assert(!published);
    assert(revisions.accepts(revisions.capture()));
  }
}
