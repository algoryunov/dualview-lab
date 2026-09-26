#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

namespace dualview::transport {
// Caller serializes access. Replies have priority; telemetry is replaceable.
// Failure is terminal: the peer must disconnect, never re-execute a command.
class DataOutbox {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::size_t buffer_limit = 256 * 1024;
  static constexpr std::size_t reply_limit = 32;
  static constexpr std::size_t byte_limit = 64 * 1024;
  struct Counters {
    std::uint64_t telemetry_skipped = 0, send_errors = 0, reply_failures = 0, replies_sent = 0;
  } counters;
  bool can_accept_command() const {
    return !failed_ && replies_.size() < reply_limit && bytes_ < byte_limit;
  }
  bool reply(std::string text, Clock::time_point now = Clock::now()) {
    if (!can_accept_command() || text.size() > byte_limit - bytes_) return fail();
    bytes_ += text.size();
    replies_.push_back({std::move(text), now});
    return true;
  }
  template <class Send>
  bool flush(bool open, std::size_t buffered, Send send, Clock::time_point now = Clock::now()) {
    if (failed_) return false;
    if (!replies_.empty() && now - replies_.front().queued >= std::chrono::seconds(5))
      return fail();
    while (open && !replies_.empty()) {
      const auto& text = replies_.front().text;
      if (buffered > buffer_limit || text.size() > buffer_limit - buffered) break;
      if (!send(text)) {
        ++counters.send_errors;
        return fail();
      }
      buffered += text.size();
      bytes_ -= text.size();
      replies_.pop_front();
      ++counters.replies_sent;
    }
    return true;
  }
  template <class Send>
  void telemetry(const std::string& text, bool open, std::size_t buffered, Send send) {
    if (failed_ || !open || !replies_.empty() || buffered > buffer_limit ||
        text.size() > buffer_limit - buffered) {
      ++counters.telemetry_skipped;
      return;
    }
    if (!send(text)) {
      ++counters.send_errors;
      ++counters.telemetry_skipped;
    }
  }
  bool fail() {
    if (!failed_) ++counters.reply_failures;
    failed_ = true;
    replies_.clear();
    bytes_ = 0;
    return false;
  }
  bool failed() const { return failed_; }
  std::size_t pending() const { return replies_.size(); }
  std::size_t bytes() const { return bytes_; }

 private:
  struct Reply {
    std::string text;
    Clock::time_point queued;
  };
  std::deque<Reply> replies_;
  std::size_t bytes_ = 0;
  bool failed_ = false;
};
}  // namespace dualview::transport
