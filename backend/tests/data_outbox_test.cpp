#include "dualview/data_outbox.hpp"

#include <cassert>
#include <iostream>
#include <vector>
using dualview::transport::DataOutbox;
using namespace std::chrono_literals;
int main() {
  const auto now = DataOutbox::Clock::now();
  DataOutbox out;
  std::vector<std::string> sent;
  auto send = [&](const std::string& text) {
    sent.push_back(text);
    return true;
  };
  out.telemetry("old", true, DataOutbox::buffer_limit, send);
  assert(out.counters.telemetry_skipped == 1 && sent.empty());
  assert(out.reply("reply1", now) && out.reply("reply2", now));
  out.telemetry("new", true, 0, send);
  assert(out.counters.telemetry_skipped == 2 && sent.empty());
  assert(out.flush(false, 0, send, now + 4s) && out.pending() == 2);
  assert(out.flush(true, DataOutbox::buffer_limit - 5, send, now + 4s) && sent.empty());
  assert(out.flush(true, 0, send, now + 4s));
  assert((sent == std::vector<std::string>{"reply1", "reply2"}));
  assert(out.pending() == 0 && out.bytes() == 0 && out.counters.replies_sent == 2);
  out.telemetry("latest", true, 0, send);
  assert(sent.back() == "latest");
  assert(out.reply("expired", now));
  assert(!out.flush(false, 0, send, now + 5s));
  assert(out.failed() && out.counters.reply_failures == 1 && !out.can_accept_command());
  DataOutbox overflow;
  for (std::size_t i = 0; i < DataOutbox::reply_limit; ++i) assert(overflow.reply("ok", now));
  assert(!overflow.can_accept_command() && !overflow.reply("overflow", now));
  DataOutbox byte_overflow;
  assert(!byte_overflow.reply(std::string(DataOutbox::byte_limit + 1, 'x'), now));
  DataOutbox broken;
  broken.reply("reply", now);
  assert(!broken.flush(true, 0, [](const auto&) { return false; }, now));
  assert(broken.counters.send_errors == 1 && broken.counters.reply_failures == 1);
  // Slow-client experiment: 8 independent peers, 10,000 skipped snapshots each,
  // then recovery. No replaceable telemetry is retained in application storage.
  std::vector<DataOutbox> peers(8);
  for (auto& peer : peers) peer.reply("completion", now);
  for (int tick = 0; tick < 10000; ++tick)
    for (auto& peer : peers) {
      peer.telemetry(std::string(4096, 't'), true, DataOutbox::buffer_limit, send);
      assert(peer.pending() == 1 && peer.bytes() == 10);
    }
  for (auto& peer : peers) {
    assert(peer.counters.telemetry_skipped == 10000);
    assert(peer.flush(true, 0, send, now + 1s));
    assert(peer.pending() == 0 && peer.bytes() == 0);
  }
  std::cout << "8 slow peers: 80,000 telemetry skips, reply storage bounded at 80 bytes total, "
               "recovery verified\n";
}
