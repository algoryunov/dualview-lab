# Tracking timing and quality

The runtime keeps up to eight frames per camera, covering at most 300 ms. It anchors the
newest frame of the slower stream and chooses the closest frame from the other camera.
Selected frames must be at most 250 ms old. Pairing does not wait for new frames, and never
rewinds a previously processed camera sequence. Preview video still uses the latest frame.
Tracking rejects frames older than 250 ms, including time spent in inference. Calibration
retains its own capture checks. The pair error threshold remains 80 ms by default.

## Timing limits

`timing_error_ms` measures the difference between receiver arrival times, optionally adjusted
by a manually measured offset. It is **not** exposure-time synchronization. Phone frames are
stamped after WebRTC decoding; laptop frames are stamped after capture returns. Camera,
encoder, network and decoder delays can differ even when this metric is small.

The receiver also records media PTS and lateness against the GStreamer pipeline clock when
available. PTS belongs to that stream's time domain; it must not be compared directly with the
laptop clock. Receiver lateness is not end-to-end capture latency. `capture_sync_verified`
remains false because the current sources do not expose a verified common exposure clock.
For true capture synchronization, both capture paths need per-frame sensor timestamps,
clock offset/uncertainty estimation, and a mapping from phone RTP timestamps to those times.

`DUALVIEW_PHONE_TIME_OFFSET_MS` (default 0, range −300–300) subtracts a measured delivery offset
from phone arrival time for pairing and freshness. Leave it at zero unless a controlled
experiment establishes a stable offset. It cannot remove variable network jitter or verify
capture synchronization. Corrected frame times still have a 250 ms age limit.

## Quality panel and controlled experiment

Open **Setup → Tracking quality**. Missing transport metrics are shown as `—`, never zero.
Network jitter and received packet loss come from receiver WebRTC stats. Round-trip and
average encoding time come from phone stats; round-trip is not one-way camera latency.
Jitter-buffer delay is a cumulative average where supported. Stats never interrupt video.

1. Keep both cameras fixed and ensure the saved intrinsic profiles match their current modes.
2. Run **Test stationary hand**, holding one hand still in both views for 20 seconds.
3. Run **Test moving hand**, moving that hand slowly sideways for 20 seconds without moving
   either camera. Repeat with faster movement if necessary.
4. Generate the report:

   ```sh
   node scripts/analyze-tracking.mjs --output logs/tracking-audit.json
   ```

Each test is labelled `stationary` or `moving` in the log and ends automatically. Test counts
are per processed sample; ordinary log samples are event-driven and must not be presented
as percentages of successful video frames. No camera images are saved.

A high stationary reprojection error suggests checking camera placement, profiles and
landmark accuracy first. Error that grows mainly during movement suggests temporal
misalignment, motion blur or unstable landmarks; the result alone cannot distinguish them.
Do not increase geometry or synchronization thresholds simply to hide rejected samples.

## Log fields

- `source`: `measured`, `predicted`, or `missing` (`held` on older servers). Predictions bridge short gaps;
  they do not count as successful new measurements.
- `timing_error_ms`: corrected receiver pair difference; null when either frame is missing.
- `arrival_delta_ms`: uncorrected receiver difference.
- Per camera: `sequence`, `sample_age_ms`, corrected `pair_sample_age_ms`, `media_pts_ms`,
  `receiver_lateness_ms`.
- Processing: `inference_calls`, `inference_cache_hits`, `processed_pairs`, `reason_counts`.
- `phone_transport`: supported packet loss, jitter, round-trip, decode/drop and encoding
  counters. Counters are session-scoped and cleared on phone disconnect.
- `phone_connection` events: signaling close, peer close, transport error, phone camera end,
  user stop or reported connection failure. A hard network loss may prevent the phone's
  final event from arriving; the server-side close event remains available.

Frame detections are cached by camera sequence and invalidated on phone reconnect.
Continuous coordinate changes no longer cause extra ordinary log entries; status changes
remain logged, and labelled tests sample at up to 10 Hz.

## Estimate delay from palm motion

Use **Measure delay · 20 s** in Tracking quality. Keep one open palm visible in both cameras,
move sideways with varying speed and brief pauses, and keep depth and orientation stable.
The runtime collects only the horizontal palm-center coordinate and receiver timestamp from
unique, confident, single-hand detections. It searches relative offsets from -150 to +150 ms
at 5 ms intervals, interpolating across gaps no longer than 150 ms. The 5 ms grid is search
resolution, not guaranteed measurement accuracy.

Both halves must agree within 25 ms and correlate strongly. Static motion, inadequate data,
weak improvement over zero delay and boundary estimates are rejected. Perspective changes,
blur or periodic motion can still bias this estimate; it is not a sensor timestamp measurement.
The image-based result estimates an effective relative visual delay, including capture,
encoding and transport, under the conditions of this test.

A ready result enables **Apply and test movement · 20 s**. This applies the signed offset only
for this server session and starts a labelled moving-hand trial. Repeat the same motion.
**Reset delay compensation** restores zero. Results expire after five minutes; reconnecting
the phone invalidates the estimate and restores the configured startup offset.
The log report separates moving trials by offset in `compensated_movement`, alongside
`timing_estimates`. Compare geometry error and successful measurements with zero offset;
a high motion correlation alone does not prove improved 3D tracking. No video is recorded.

## Concurrency and geometry evidence

`processing.discarded_work` counts processing completions invalidated by phone, timing or calibration changes. `processing.frame_input` exposes bounded history depth, evictions and maximum frame-publication lock wait. These are software receiver measurements, not physical exposure timing. Diagnostic collection and estimate expiry belong to `DiagnosticSession`; a cancelled/replaced timing computation cannot publish its old estimate.

See [Runtime ownership](runtime-ownership.md) for ordering rules and [geometry evaluation](evaluation.md) for controlled noise, baseline and hidden timing-error experiments. The recorded synthetic geometry results must not be reported as real-hand accuracy.
