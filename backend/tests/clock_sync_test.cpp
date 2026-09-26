#include "dualview/clock_sync.hpp"

#include <cassert>
#include <limits>
using dualview::ClockSync;
int main() {
  ClockSync clock;
  assert(clock.add(0, 20, 1010, 1010));
  assert(!clock.best(20));
  assert(clock.add(100, 140, 1110, 1110));
  assert(clock.add(200, 210, 1205, 1205));
  auto sample = clock.best(210);
  assert(sample && sample->offset == 1000 && sample->rtt == 10);
  assert(!clock.best(11000));
  assert(!clock.add(300, 290, 1300, 1300));
  assert(!clock.add(300, 310, 1300, 1320));
  assert(!clock.add(300, 310, std::numeric_limits<double>::infinity(), 1320));
  assert(clock.add(300, 310, 2305, 2305));
  assert(!clock.best(310));  // A clock jump must warm up again.
}
