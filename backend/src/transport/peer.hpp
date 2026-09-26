#pragma once
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
#include <libsoup/soup.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>

#include "dualview/clock_sync.hpp"
#include "dualview/data_outbox.hpp"
#include "dualview/runtime.hpp"
namespace dualview::transport {
// Owned by the server loop. close() stops streaming before ownership is released.
class Peer : public std::enable_shared_from_this<Peer> {
 public:
  Peer(Runtime&, SoupWebsocketConnection*, bool phone, int preview_bitrate);
  ~Peer();
  bool is_phone() const { return phone; }
  bool is_closed() const { return closed; }
  void close();
  void tick();

 private:
  Runtime& runtime;
  int preview_bitrate;
  SoupWebsocketConnection* socket;
  GstElement* pipeline = nullptr;
  GstElement* rtc = nullptr;
  GstElement* sources[2]{};
  GstWebRTCDataChannel* channel = nullptr;
  std::mutex channel_mutex, signal_mutex, command_mutex;
  DataOutbox outbox;
  std::deque<std::string> outgoing;
  std::deque<std::pair<unsigned, std::string>> remote_candidates;
  std::atomic<bool> remote_ready{false};
  bool phone = false, offered = false;
  std::atomic<bool> closed{false};
  std::uint64_t previous[2]{};
  guint bus_watch = 0;
  Clock::time_point last_stats{};
  std::atomic<bool> stats_pending{false};

  ClockSync clock_sync;
  Clock::time_point last_clock_ping{};
  double clock_sent_ms = 0;
  std::uint64_t clock_nonce = 0;
  bool clock_pending = false;
  bool clock_supported = false;
  std::atomic<std::uint64_t> reference_packets{0};
  bool stopped = false;
  void signal(Json value);
  void error(const std::string& message);
  void data(Json value);
  void offer(const std::string& sdp);
  void receive_signal(const Json& message);
};
}  // namespace dualview::transport
