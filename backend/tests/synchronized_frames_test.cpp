#include "dualview/synchronized_frames.hpp"

#include <cassert>
using namespace dualview;
using namespace std::chrono_literals;
int main() {
  const auto now = Clock::now();
  const auto frame = [&](int ms, std::uint64_t sequence) {
    return CameraFrame{
        cv::Mat::zeros(2, 2, CV_8UC3), now + std::chrono::milliseconds(ms), sequence, {}, {}};
  };
  std::deque<CameraFrame> history[2];
  assert(!closest_pair(history, now, 0, 80));
  remember_frame(history[0], frame(-110, 1));
  remember_frame(history[0], frame(-70, 2));
  remember_frame(history[0], frame(-30, 3));
  remember_frame(history[0], frame(0, 4));
  remember_frame(history[1], frame(-65, 1));
  auto pair = closest_pair(history, now, 0, 80);
  assert(pair && (*pair)[0].sequence == 2);  // 5 ms instead of latest/latest 65 ms.
  assert(!closest_pair(history, now, 0, 4));
  assert(!closest_pair(history, now + 300ms, 0, 80));
  remember_frame(history[1], frame(0, 2));
  pair = closest_pair(history, now, 65, 80);
  assert(pair && (*pair)[0].sequence == 2 && (*pair)[1].sequence == 2);
  pair = closest_pair(history, now, -65, 80);
  assert(pair && (*pair)[1].sequence == 1);  // negative offset chooses an older phone frame
  history[1].clear();
  assert(!closest_pair(history, now, 0, 80));
  for (int i = 0; i < 100; ++i) remember_frame(history[0], frame(i * 10, i + 5));
  assert(history[0].size() == 8);
  remember_frame(history[0], frame(2000, 105));
  assert(history[0].size() == 1);
  FrameStore store;
  const std::uint64_t previous[2]{};
  for (int i = 0; i < 100; ++i) store.publish(0, frame(-100 + i, i));
  store.publish(1, frame(-3, 0));
  assert(store.latest()[0].sequence == 100 && store.latest()[1].sequence == 1);
  auto selected = store.select(now, 0, 80, previous);
  assert(selected[0].sequence == 98);
  auto held = store.latest()[1];
  auto generation = store.phone_generation();
  store.clear_phone();
  store.publish(1, frame(0, 0),
                generation);  // A clone started before disconnect cannot resurrect it.
  assert(store.latest()[1].image.empty());
  assert(!held.image.empty());  // Readers own their image reference independently.
  assert(store.metrics().depth[0] == 8 && store.metrics().evicted[0] == 92);
  store.publish(1, frame(0, 0), store.phone_generation());
  assert(store.latest()[1].sequence == 2);
  DetectionCache<int> cache;
  int calls = 0;
  bool hit;
  auto detect = [&] { return ++calls; };
  assert(cache.get(1, 0, detect, hit) == 1 && !hit);
  assert(cache.get(1, 0, detect, hit) == 1 && hit && calls == 1);
  assert(cache.get(2, 0, detect, hit) == 2 && !hit);
  assert(cache.get(1, 1, detect, hit) == 3 && !hit);  // reconnect invalidates cache
  for (int i = 2; i < 12; ++i) cache.get(i, 1, detect, hit);
  cache.get(1, 1, detect, hit);
  assert(!hit);  // bounded cache eviction
}
