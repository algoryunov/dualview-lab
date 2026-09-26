#pragma once
#include <cstdint>
namespace dualview {
// Not a mutex. Capture, transitions, and accepts() all require the owner's state lock.
// An operation checks its token while holding that same lock through publication.
class WorkRevisions {
 public:
  struct Token {
    std::uint64_t phone = 0, timing = 0, calibration = 0;
    bool operator==(const Token&) const = default;
  };
  Token capture() const { return current_; }
  bool accepts(Token token) const { return token == current_; }
  void phone_replaced() {
    ++current_.phone;
    ++current_.timing;
    ++current_.calibration;
  }
  void timing_changed() { ++current_.timing; }
  void calibration_changed() { ++current_.calibration; }

 private:
  Token current_;
};
}  // namespace dualview
