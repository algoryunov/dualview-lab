# Realtime protocol v1

## HTTPS bootstrap

- `GET /api/health`: C++ server health and transport name.
- `GET /api/config`: `show_qr_code`, `pairing_required`, `public_origin`.
- `POST /api/session`: `phone_url` and `expires_in_seconds`; creates a five-minute token when pairing is required.

## WebSocket signaling

Connect to `/ws/rtc?role=dashboard`, or `/ws/rtc?role=phone&session=TOKEN`. Only the phone role needs a pairing token. Dashboard controls are intended for the trusted local LAN.

Browser sends `{ "type": "offer", "sdp": "..." }`; server replies with `{ "type": "answer", "sdp": "...", "sdpType": "answer" }`. Both exchange `{ "type": "candidate", "candidate": "...", "sdpMLineIndex": 0 }`. Errors use `{ "type": "error", "message": "..." }`. Reconnect to renegotiate.

## DataChannel

The dashboard creates an ordered channel named `control`. Server telemetry is `{ "type": "telemetry", "payload": { ... } }`; the payload matches `frontend/src/types.ts` and includes camera projection parameters.

Commands include a unique `id`:

```json
{ "id": "unique-id", "command": "intrinsics.start", "camera": "phone" }
```

Supported commands: `intrinsics.start` (`camera`: laptop/phone), `calibration.start`, `calibration.cancel`, `interaction.model` (`model_id`: car/bed/flower/tower), `interaction.reset`.

`metal.sample` accepts a bounded `sample` object from the main metal renderer: `mode`, `phase`, `camera`, `client_ms`, `dt_ms`, `hands`, `distance_m`, `separation`, `neck`, `opacity`, `center_m` (3 numbers), `axis` (3), `blobs` (20), and `velocities` (15). The backend validates the payload and rate-limits writes to the separate metal log. The browser samples at 10 Hz with at most one diagnostic request in flight; it drops samples while disconnected or waiting for acknowledgement.

Hand telemetry distinguishes `source` and `secondary_source` as `measured`, `predicted`, or `missing` (older servers may send `held`), with per-hand `hold_age_ms`. Predicted poses last at most 500 ms from the last valid reconstruction and never refresh their own timestamp. Damped velocity extrapolation is capped at 3 cm. An existing grab may translate using the predicted cursor; new gestures, rotation, and scaling are suspended while either pose is predicted. Camera disconnect, stale frames, calibration invalidation, and processing errors clear retained poses. Pinching activates model movement directly; both hands must pinch to scale, with release hysteresis on each hand.

Success: `{ "type": "response", "id": "unique-id", "payload": { "ok": true } }`. Failure: `{ "type": "response", "id": "unique-id", "error": "..." }`. Phone peers cannot issue dashboard commands. Browser requests time out after ten seconds. Calibration completion arrives in telemetry; the response acknowledges capture start.

## Timing diagnostics

`diagnostics.phase` accepts `phase: stationary | moving | idle`. Non-idle phases require fresh
frames from both cameras, label tracking samples for 20 seconds, and expose `diagnostic_counts`
in processing telemetry. `idle` stops the test. This records measurements, not video.

Phone signaling may include `phone_stats` with a `values` object containing finite nonnegative
`round_trip_ms` and `phone_encode_ms`. `phone_event` accepts `camera_ended`, `user_stop`,
`connection_failed`, or `connection_disconnected`. These messages do not grant dashboard
command access. Receiver stats are collected independently by the server.

See [tracking diagnostics](tracking-diagnostics.md) for clock domains and measurement limits.

New server SDP answers include `diagnostics: true`. Phone clients send optional diagnostic
messages only after this capability is advertised, preserving compatibility with older servers.

`diagnostics.phase` additionally accepts `timing`, collecting a 20-second single-palm motion
trace. `processing.timing_estimate` reports collecting/ready/unreliable/cancelled plus the
signed offset, motion correlations and rejection reason. `timing.apply` requires a ready,
recent result and fresh cameras; it starts a compensated 20-second moving trial. `timing.reset`
restores zero offset. Both reject changes during an active test or calibration. The tracking
worker discards in-flight results when the timing generation changes.

### Phone clock diagnostics

A phone offer may advertise `clockSync: 1`. The server then sends one
`clock_ping` per second with an integer `id`. The phone immediately replies
with `clock_reply`, the same `id`, and Unix millisecond `received` / `sent`
timestamps. Replies bypass asynchronous SDP processing. Only the outstanding
request is accepted. Older clients receive no probes.

The server uses a four-timestamp estimate, removes phone processing time,
and selects the minimum-RTT sample from at most eight recent samples. Three
samples are required, samples expire after ten seconds, and a large clock
jump restarts estimation. `clock_sync_ready` gates `clock_uncertainty_ms`;
the latter is an estimate, not a guarantee of symmetric network delay.

GStreamer reference timestamp metadata is enabled when supported, and
`sender_reference_packets` counts incoming RTP buffers containing it.
This proves metadata availability only: it does not yet bind sender time
to decoded frames or establish sensor exposure time. Pairing remains on
receiver time; clock estimates never silently apply a video offset.

### Command delivery under congestion

Telemetry is replaceable and may be skipped. Command replies have FIFO priority and an application queue bounded to 32 replies / 64 KiB with a five-second queue-wait deadline. New commands are not executed when that queue cannot admit another reply. Reply overflow, expiry, or a send error terminates the connection explicitly. A disconnected or timed-out command may already have executed: inspect the current state before retrying; the transport never replays state-changing commands automatically.

Optional `processing.data_channel` counters are per dashboard peer: `telemetry_skipped`, `send_errors`, `reply_failures`, `replies_sent`, `pending_replies`, `pending_reply_bytes`, and `buffered_bytes`. Optional `processing.frame_input` contains two-camera `history_depth`, `evicted_frames`, and `max_lock_wait_us`; `geometry_duration_ms` and `frame_to_result_age_ms` expose processing duration and receive-to-result age, not sensor exposure latency.

The wire adapter validates commands in `runtime_protocol.cpp`, converts them to a typed `RuntimeCommand`, and serializes a typed `CommandReply`; clients retain the existing JSON request/reply format. `processing.discarded_work` counts processing completions rejected because their phone, timing or calibration revision changed. Snapshot hand/interaction data is serialized from typed domain state, without an independently mutable JSON copy.
