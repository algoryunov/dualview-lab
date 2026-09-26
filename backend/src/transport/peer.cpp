#include "peer.hpp"

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>

#include "dualview/runtime_protocol.hpp"
namespace dualview::transport {
Peer::Peer(Runtime& r, SoupWebsocketConnection* s, bool p, int bitrate)
    : runtime(r),
      preview_bitrate(bitrate),
      socket(SOUP_WEBSOCKET_CONNECTION(g_object_ref(s))),
      phone(p) {
  soup_websocket_connection_set_max_incoming_payload_size(socket, 128 * 1024);
  g_signal_connect(
      socket, "message",
      G_CALLBACK(+[](SoupWebsocketConnection*, gint type, GBytes* bytes, gpointer ptr) {
        auto* peer = static_cast<Peer*>(ptr);
        if (type != SOUP_WEBSOCKET_DATA_TEXT || peer->closed) return;
        try {
          gsize size = 0;
          const auto* text = static_cast<const char*>(g_bytes_get_data(bytes, &size));
          auto message = Json::parse(text, text + size);
          peer->receive_signal(message);
        } catch (const std::exception& e) {
          peer->error(e.what());
        }
      }),
      this);
  g_signal_connect(socket, "closed", G_CALLBACK(+[](SoupWebsocketConnection*, gpointer ptr) {
                     auto* peer = static_cast<Peer*>(ptr);
                     if (peer->phone) peer->runtime.transport_event("signaling_closed");
                     peer->closed = true;
                   }),
                   this);
}

void Peer::signal(Json value) {
  std::lock_guard lock(signal_mutex);
  outgoing.push_back(value.dump());
}

void Peer::error(const std::string& message) {
  if (phone) runtime.transport_event("transport_error: " + message);
  signal({{"type", "error"}, {"message", message}});
}

Peer::~Peer() {
  close();
  {
    std::lock_guard lock(channel_mutex);
    if (channel) {
      g_signal_handlers_disconnect_by_data(channel, this);
      g_object_unref(channel);
    }
  }
  for (auto* source : sources)
    if (source) gst_object_unref(source);
  if (rtc) gst_object_unref(rtc);
  if (pipeline) gst_object_unref(pipeline);
  g_signal_handlers_disconnect_by_data(socket, this);
  g_object_unref(socket);
}

void Peer::data(Json value) {
  std::lock_guard lock(channel_mutex);
  if (closed) return;
  const bool reply = !value.is_null() && value.value("type", "") == "response";
  if (reply && !outbox.reply(value.dump())) return;
  GstWebRTCDataChannelState state = GST_WEBRTC_DATA_CHANNEL_STATE_CLOSED;
  guint64 buffered = 0;
  if (channel) {
    g_object_get(channel, "ready-state", &state, "buffered-amount", &buffered, nullptr);
  }
  const bool open = state == GST_WEBRTC_DATA_CHANNEL_STATE_OPEN;
  const auto send = [&](const std::string& text) {
    GError* error = nullptr;
    const bool sent = gst_webrtc_data_channel_send_string_full(channel, text.c_str(), &error);
    if (error) {
      g_error_free(error);
      return false;
    }
    return sent;
  };
  if (!outbox.flush(open, buffered, send)) return;
  if (!reply && !value.is_null()) {
    if (channel) g_object_get(channel, "buffered-amount", &buffered, nullptr);
    const auto& counters = outbox.counters;
    value["payload"]["processing"]["data_channel"] = {
        {"telemetry_skipped", counters.telemetry_skipped},
        {"send_errors", counters.send_errors},
        {"reply_failures", counters.reply_failures},
        {"replies_sent", counters.replies_sent},
        {"pending_replies", outbox.pending()},
        {"pending_reply_bytes", outbox.bytes()},
        {"buffered_bytes", buffered}};
    outbox.telemetry(value.dump(), open, buffered, send);
  }
}

void Peer::offer(const std::string& sdp) {
  if (offered) throw std::runtime_error("Renegotiation requires reconnecting");
  offered = true;
  std::string description = "webrtcbin name=rtc bundle-policy=max-bundle latency=80";
  if (!phone)
    for (int i = 0; i < 2; ++i)
      description += " appsrc name=source" + std::to_string(i) +
                     " is-live=true format=time do-timestamp=true max-buffers=1 "
                     "leaky-type=downstream ! queue max-size-buffers=1 leaky=downstream ! "
                     "videoconvert ! video/x-raw,format=I420 ! vp8enc deadline=1 cpu-used=4 "
                     "threads=2 lag-in-frames=0 "
                     "target-bitrate=" +
                     std::to_string(preview_bitrate) +
                     " keyframe-max-dist=30 ! rtpvp8pay pt=96 ssrc=" + std::to_string(1001 + i) +
                     " ! "
                     "application/"
                     "x-rtp,media=video,encoding-name=VP8,payload=96,clock-rate=90000,ssrc=(uint)" +
                     std::to_string(1001 + i) + " ! rtc.sink_" + std::to_string(i);
  GError* error = nullptr;
  pipeline = gst_parse_launch(description.c_str(), &error);
  if (error) {
    std::string message = error->message;
    g_error_free(error);
    throw std::runtime_error(message);
  }
  if (!GST_IS_PIPELINE(pipeline)) {
    auto* bin = gst_pipeline_new(nullptr);
    gst_bin_add(GST_BIN(bin), pipeline);
    pipeline = bin;
  }
  // Native RTP/RTCP sender-clock metadata; no additional video protocol or buffering.
  g_signal_connect(pipeline, "deep-element-added",
                   G_CALLBACK(+[](GstBin*, GstBin*, GstElement* element, gpointer) {
                     if (g_object_class_find_property(G_OBJECT_GET_CLASS(element),
                                                      "add-reference-timestamp-meta"))
                       g_object_set(element, "add-reference-timestamp-meta", TRUE, nullptr);
                   }),
                   nullptr);
  rtc = gst_bin_get_by_name(GST_BIN(pipeline), "rtc");
  if (!rtc) throw std::runtime_error("WebRTC element unavailable");
  g_signal_connect(
      rtc, "on-ice-candidate",
      G_CALLBACK(+[](GstElement*, guint index, gchar* candidate, gpointer ptr) {
        static_cast<Peer*>(ptr)->signal(
            {{"type", "candidate"}, {"candidate", candidate}, {"sdpMLineIndex", index}});
      }),
      this);
  g_signal_connect(
      rtc, "on-data-channel", G_CALLBACK(+[](GstElement*, GstWebRTCDataChannel* ch, gpointer ptr) {
        auto* self = static_cast<Peer*>(ptr);
        {
          std::lock_guard lock(self->channel_mutex);
          if (self->channel) return;
          self->channel = GST_WEBRTC_DATA_CHANNEL(g_object_ref(ch));
        }
        g_signal_connect(
            ch, "on-message-string",
            G_CALLBACK(+[](GstWebRTCDataChannel*, gchar* text, gpointer data) {
              auto* peer = static_cast<Peer*>(data);
              std::lock_guard command_lock(peer->command_mutex);
              {
                std::lock_guard channel_lock(peer->channel_mutex);
                if (peer->closed || !peer->outbox.can_accept_command()) {
                  peer->outbox.fail();
                  return;
                }
              }
              Json id = nullptr;
              try {
                if (peer->phone)
                  throw std::runtime_error("Phone peers cannot issue dashboard commands");
                auto request = Json::parse(text);
                id = request.at("id");
                if (!id.is_string() || id.get_ref<const std::string&>().size() > 128)
                  throw std::invalid_argument("Invalid command id");
                auto result = dispatch_command(peer->runtime, request);
                peer->data({{"type", "response"}, {"id", id}, {"payload", result}});
              } catch (const std::exception& e) {
                peer->data({{"type", "response"}, {"id", id}, {"error", e.what()}});
              }
            }),
            self);
      }),
      this);
  if (phone)
    g_signal_connect(
        rtc, "pad-added", G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer ptr) {
          if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) return;
          auto* self = static_cast<Peer*>(ptr);
          if (self->closed) return;
          gst_pad_add_probe(
              pad, GST_PAD_PROBE_TYPE_BUFFER,
              +[](GstPad*, GstPadProbeInfo* info, gpointer ptr) -> GstPadProbeReturn {
                auto* peer = static_cast<Peer*>(ptr);
                auto* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
                if (buffer && gst_buffer_get_reference_timestamp_meta(buffer, nullptr))
                  ++peer->reference_packets;
                return GST_PAD_PROBE_OK;
              },
              self, nullptr);
          GError* error = nullptr;
          auto* decode = gst_parse_bin_from_description(
              // Drop only complete decoded frames. Dropping RTP fragments before depayloading
              // corrupts VP8 frames and produces block artifacts during bursts.
              // webrtcbin already reorders incoming RTP using its own jitter buffer.
              "rtpvp8depay request-keyframe=true wait-for-keyframe=true ! vp8dec ! "
              "queue max-size-buffers=1 leaky=downstream ! videoconvert ! "
              "video/x-raw,format=BGR ! appsink name=frames emit-signals=true sync=false "
              "max-buffers=1 drop=true",
              true, &error);
          if (error) {
            self->error(error->message);
            g_error_free(error);
            return;
          }
          auto* sink = gst_bin_get_by_name(GST_BIN(decode), "frames");
          g_signal_connect(
              sink, "new-sample", G_CALLBACK(+[](GstAppSink* sink, gpointer data) -> GstFlowReturn {
                auto* peer = static_cast<Peer*>(data);
                if (peer->closed) return GST_FLOW_FLUSHING;
                auto* sample = gst_app_sink_pull_sample(sink);
                if (!sample) return GST_FLOW_EOS;
                GstVideoInfo info;
                GstVideoFrame frame;
                if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) &&
                    gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample),
                                        GST_MAP_READ)) {
                  if (GST_VIDEO_INFO_WIDTH(&info) > 4096 || GST_VIDEO_INFO_HEIGHT(&info) > 4096) {
                    gst_video_frame_unmap(&frame);
                    gst_sample_unref(sample);
                    return GST_FLOW_ERROR;
                  }
                  cv::Mat image(GST_VIDEO_INFO_HEIGHT(&info), GST_VIDEO_INFO_WIDTH(&info), CV_8UC3,
                                GST_VIDEO_FRAME_PLANE_DATA(&frame, 0),
                                static_cast<std::size_t>(GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0)));
                  auto* buffer = gst_sample_get_buffer(sample);
                  const auto pts = GST_BUFFER_PTS(buffer);
                  std::optional<double> media_pts;
                  std::optional<double> lateness;
                  if (GST_CLOCK_TIME_IS_VALID(pts)) {
                    media_pts = double(pts) / GST_MSECOND;
                    const auto* segment = gst_sample_get_segment(sample);
                    const auto running =
                        segment && segment->format == GST_FORMAT_TIME
                            ? gst_segment_to_running_time(segment, GST_FORMAT_TIME, pts)
                            : GST_CLOCK_TIME_NONE;
                    auto* clock = gst_element_get_clock(peer->pipeline);
                    if (clock && GST_CLOCK_TIME_IS_VALID(running)) {
                      const auto now = gst_clock_get_time(clock);
                      const auto base = gst_element_get_base_time(peer->pipeline);
                      if (now >= base && now - base >= running)
                        lateness = double(now - base - running) / GST_MSECOND;
                    }
                    if (clock) gst_object_unref(clock);
                  }
                  peer->runtime.receive_phone(image, media_pts, lateness);
                  gst_video_frame_unmap(&frame);
                }
                gst_sample_unref(sample);
                return GST_FLOW_OK;
              }),
              self);
          gst_object_unref(sink);
          gst_bin_add(GST_BIN(self->pipeline), decode);
          auto* sinkpad = gst_element_get_static_pad(decode, "sink");
          const auto link_result = gst_pad_link(pad, sinkpad);
          if (link_result != GST_PAD_LINK_OK)
            self->error(std::string("Could not link incoming video: ") +
                        gst_pad_link_get_name(link_result));
          gst_object_unref(sinkpad);
          gst_element_sync_state_with_parent(decode);
        }),
        this);
  if (!phone)
    for (int i = 0; i < 2; ++i)
      sources[i] = gst_bin_get_by_name(GST_BIN(pipeline), ("source" + std::to_string(i)).c_str());
  auto* bus = gst_element_get_bus(pipeline);
  bus_watch = gst_bus_add_watch(
      bus,
      +[](GstBus*, GstMessage* message, gpointer ptr) -> gboolean {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
          GError* e = nullptr;
          gchar* debug = nullptr;
          gst_message_parse_error(message, &e, &debug);
          static_cast<Peer*>(ptr)->error(e->message);
          g_error_free(e);
          g_free(debug);
        }
        return G_SOURCE_CONTINUE;
      },
      this);
  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  if (!phone)
    for (int i = 0; i < 2; ++i) {
      auto* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGR", "width",
                                       G_TYPE_INT, 640, "height", G_TYPE_INT, 360, "framerate",
                                       GST_TYPE_FRACTION, 15, 1, nullptr);
      gst_app_src_set_caps(GST_APP_SRC(sources[i]), caps);
      gst_caps_unref(caps);
      auto* buffer = gst_buffer_new_allocate(nullptr, 640 * 360 * 3, nullptr);
      gst_buffer_memset(buffer, 0, 0, 640 * 360 * 3);
      gst_app_src_push_buffer(GST_APP_SRC(sources[i]), buffer);
    }
  GstSDPMessage* message = nullptr;
  gst_sdp_message_new(&message);
  if (gst_sdp_message_parse_buffer(reinterpret_cast<const guint8*>(sdp.data()), sdp.size(),
                                   message) != GST_SDP_OK) {
    gst_sdp_message_free(message);
    throw std::runtime_error("Invalid SDP");
  }
  auto* remote = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, message);
  auto* promise = gst_promise_new_with_change_func(
      +[](GstPromise* promise, gpointer ptr) {
        auto self = static_cast<std::weak_ptr<Peer>*>(ptr)->lock();
        const auto completion = gst_promise_wait(promise);
        const auto* reply =
            completion == GST_PROMISE_RESULT_REPLIED ? gst_promise_get_reply(promise) : nullptr;
        GError* failure = nullptr;
        if (reply && gst_structure_has_field(reply, "error"))
          gst_structure_get(reply, "error", G_TYPE_ERROR, &failure, nullptr);
        const bool accepted = completion == GST_PROMISE_RESULT_REPLIED && !failure;
        const std::string message =
            failure ? failure->message : "Remote description was not accepted";
        if (failure) g_error_free(failure);
        gst_promise_unref(promise);
        if (!self || self->closed) return;
        if (!accepted) {
          self->error(message);
          return;
        }
        self->remote_ready = true;
        if (self->closed) return;
        // Negotiate retransmission before answering. Packet loss must trigger
        // recovery rather than leave a damaged reference frame on screen.
        GArray* transceivers = nullptr;
        g_signal_emit_by_name(self->rtc, "get-transceivers", &transceivers);
        if (transceivers) {
          for (guint i = 0; i < transceivers->len; ++i) {
            auto* transceiver = g_array_index(transceivers, GstWebRTCRTPTransceiver*, i);
            g_object_set(transceiver, "do-nack", TRUE, nullptr);
          }
          g_array_unref(transceivers);
        }
        auto* answer = gst_promise_new_with_change_func(
            +[](GstPromise* promise, gpointer data) {
              auto peer = static_cast<std::weak_ptr<Peer>*>(data)->lock();
              if (!peer) {
                gst_promise_unref(promise);
                return;
              }
              GstWebRTCSessionDescription* answer = nullptr;
              const auto* reply = gst_promise_get_reply(promise);
              if (reply)
                gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer,
                                  nullptr);
              if (answer && !peer->closed) {
                g_signal_emit_by_name(peer->rtc, "set-local-description", answer, nullptr);
                auto* text = gst_sdp_message_as_text(answer->sdp);
                peer->signal({{"type", "answer"},
                              {"sdp", text},
                              {"sdpType", "answer"},
                              {"diagnostics", true}});
                g_free(text);
              } else if (!peer->closed)
                peer->error("Could not create WebRTC answer");
              if (answer) gst_webrtc_session_description_free(answer);
              gst_promise_unref(promise);
            },
            new std::weak_ptr<Peer>(self),
            +[](gpointer data) { delete static_cast<std::weak_ptr<Peer>*>(data); });
        g_signal_emit_by_name(self->rtc, "create-answer", nullptr, answer);
      },
      new std::weak_ptr<Peer>(weak_from_this()),
      +[](gpointer data) { delete static_cast<std::weak_ptr<Peer>*>(data); });
  g_signal_emit_by_name(rtc, "set-remote-description", remote, promise);
  gst_webrtc_session_description_free(remote);
}

void Peer::close() {
  if (stopped) return;
  stopped = true;
  closed = true;
  g_signal_handlers_disconnect_by_data(socket, this);
  if (soup_websocket_connection_get_state(socket) == SOUP_WEBSOCKET_STATE_OPEN)
    soup_websocket_connection_close(socket, 1000, "Peer closed");
  if (bus_watch) {
    g_source_remove(bus_watch);
    bus_watch = 0;
  }
  if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
  if (phone) {
    runtime.transport_event("peer_closed");
    runtime.clear_phone();
  }
}
void Peer::receive_signal(const Json& message) {
  if (message.at("type") == "offer") {
    clock_supported = phone && message.value("clockSync", 0) == 1;
    offer(message.at("sdp"));
  } else if (message.at("type") == "candidate") {
    if (remote_candidates.size() >= 64) throw std::runtime_error("Too many ICE candidates");
    remote_candidates.emplace_back(message.at("sdpMLineIndex").get<unsigned>(),
                                   message.at("candidate").get<std::string>());
  } else if (phone && message.at("type") == "clock_reply") {
    if (!clock_pending || message.value("id", std::uint64_t{0}) != clock_nonce) return;
    clock_pending = false;
    const double now =
        std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
    if (message.contains("received") && message["received"].is_number() &&
        message.contains("sent") && message["sent"].is_number())
      clock_sync.add(clock_sent_ms, now, message["received"].get<double>(),
                     message["sent"].get<double>());
  } else if (phone && message.at("type") == "phone_stats") {
    runtime.transport_diagnostics(message.value("values", Json::object()));
  } else if (phone && message.at("type") == "phone_event") {
    const auto reason = message.value("reason", "unknown");
    if (reason == "camera_ended" || reason == "user_stop" || reason == "connection_failed" ||
        reason == "connection_disconnected")
      runtime.transport_event(reason);
  } else
    throw std::invalid_argument("Unknown signaling message");
}
void Peer::tick() {
  if (closed) return;
  data(nullptr);
  bool failed;
  {
    std::lock_guard lock(channel_mutex);
    failed = outbox.failed();
  }
  if (failed) {
    const auto message = Json{{"type", "error"},
                              {"message",
                               "Command reply delivery failed; command outcome may be unknown. "
                               "Reconnect and inspect state before retrying."}}
                             .dump();
    if (soup_websocket_connection_get_state(socket) == SOUP_WEBSOCKET_STATE_OPEN)
      soup_websocket_connection_send_text(socket, message.c_str());
    runtime.transport_event("command_reply_delivery_failed");
    close();
    return;
  }
  {
    std::lock_guard lock(signal_mutex);
    for (const auto& text : outgoing) soup_websocket_connection_send_text(socket, text.c_str());
    outgoing.clear();
  }
  if (clock_supported && remote_ready &&
      Clock::now() - last_clock_ping >= std::chrono::seconds(1)) {
    last_clock_ping = Clock::now();
    clock_sent_ms =
        std::chrono::duration<double, std::milli>(last_clock_ping.time_since_epoch()).count();
    clock_pending = true;
    const auto ping = Json{{"type", "clock_ping"}, {"id", ++clock_nonce}}.dump();
    soup_websocket_connection_send_text(socket, ping.c_str());
    const auto sample = clock_sync.best(clock_sent_ms);
    runtime.transport_diagnostics(
        {{"clock_sync_ready", sample ? 1 : 0},
         {"clock_probe_rtt_ms", sample ? Json(sample->rtt) : Json(nullptr)},
         {"clock_uncertainty_ms", sample ? Json(sample->rtt / 2 + 1) : Json(nullptr)},
         {"sender_reference_packets", reference_packets.load()}});
  }
  if (remote_ready && rtc) {
    for (const auto& [index, candidate] : remote_candidates)
      g_signal_emit_by_name(rtc, "add-ice-candidate", index, candidate.c_str());
    remote_candidates.clear();
  }
  if (phone && rtc && Clock::now() - last_stats >= std::chrono::seconds(1) &&
      !stats_pending.exchange(true)) {
    last_stats = Clock::now();
    auto* promise = gst_promise_new_with_change_func(
        +[](GstPromise* promise, gpointer ptr) {
          auto self = static_cast<std::weak_ptr<Peer>*>(ptr)->lock();
          if (self) {
            self->stats_pending = false;
            const auto* reply = gst_promise_get_reply(promise);
            if (reply && !self->closed) {
              Json values = Json::object();
              const auto number = [](const GValue* value) -> std::optional<double> {
                if (!value) return {};
                if (G_VALUE_HOLDS_DOUBLE(value)) return g_value_get_double(value);
                if (G_VALUE_HOLDS_UINT64(value)) return double(g_value_get_uint64(value));
                if (G_VALUE_HOLDS_INT64(value)) return double(g_value_get_int64(value));
                if (G_VALUE_HOLDS_UINT(value)) return g_value_get_uint(value);
                if (G_VALUE_HOLDS_INT(value)) return g_value_get_int(value);
                return {};
              };
              for (int i = 0; i < gst_structure_n_fields(reply); ++i) {
                const auto* value =
                    gst_structure_get_value(reply, gst_structure_nth_field_name(reply, i));
                if (!value || !GST_VALUE_HOLDS_STRUCTURE(value)) continue;
                const auto* entry = gst_value_get_structure(value);
                const auto* type = gst_structure_get_value(entry, "type");
                if (!type || !G_VALUE_HOLDS_ENUM(type) ||
                    g_value_get_enum(type) != GST_WEBRTC_STATS_INBOUND_RTP)
                  continue;
                for (const auto& [source, target] : {std::pair{"packets-lost", "packets_lost"},
                                                     {"frames-dropped", "frames_dropped"},
                                                     {"frames-decoded", "frames_decoded"},
                                                     {"bytes-received", "bytes_received"}}) {
                  if (const auto v = number(gst_structure_get_value(entry, source)))
                    values[target] = *v;
                }
                if (const auto v = number(gst_structure_get_value(entry, "jitter")))
                  values["jitter_ms"] = *v * 1000;
                const auto delay = number(gst_structure_get_value(entry, "jitter-buffer-delay"));
                const auto count =
                    number(gst_structure_get_value(entry, "jitter-buffer-emitted-count"));
                if (delay && count && *count > 0)
                  values["jitter_buffer_ms"] = *delay * 1000 / *count;
              }
              self->runtime.transport_diagnostics(values);
            }
          }
          gst_promise_unref(promise);
        },
        new std::weak_ptr<Peer>(weak_from_this()),
        +[](gpointer ptr) { delete static_cast<std::weak_ptr<Peer>*>(ptr); });
    g_signal_emit_by_name(rtc, "get-stats", nullptr, promise);
  }
  if (phone || !pipeline) return;
  data({{"type", "telemetry"}, {"payload", runtime.snapshot()}});
  for (int i = 0; i < 2; ++i) {
    auto f = runtime.frame(i == 0 ? CameraId::Laptop : CameraId::Phone);
    if (f.image.empty() || previous[i] == f.sequence) continue;
    previous[i] = f.sequence;
    auto image = f.image.isContinuous() ? f.image : f.image.clone();
    auto* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGR", "width",
                                     G_TYPE_INT, image.cols, "height", G_TYPE_INT, image.rows,
                                     "framerate", GST_TYPE_FRACTION, 15, 1, nullptr);
    gst_app_src_set_caps(GST_APP_SRC(sources[i]), caps);
    gst_caps_unref(caps);
    GstVideoInfo info;
    gst_video_info_set_format(&info, GST_VIDEO_FORMAT_BGR, image.cols, image.rows);
    auto* buffer = gst_buffer_new_allocate(nullptr, info.size, nullptr);
    const auto stride = static_cast<std::size_t>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 0));
    for (int row = 0; row < image.rows; ++row)
      gst_buffer_fill(buffer, static_cast<std::size_t>(row) * stride, image.ptr(row),
                      static_cast<std::size_t>(image.cols) * 3);
    gst_app_src_push_buffer(GST_APP_SRC(sources[i]), buffer);
  }
}
}  // namespace dualview::transport
