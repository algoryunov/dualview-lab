# Backpressure and operating limits

Every stage between a camera and the rendered scene can produce faster than the next stage consumes. This document records the chosen policy at each boundary and the conditions under which the system is designed to operate. These are deliberate design decisions, not tuning parameters to be changed casually: several of them exist to keep a late frame from being presented as a fresh measurement.

## Backpressure policy

The system prefers dropping stale work over queueing it. A hand position is only useful while it is current, so every stage below is either latest-wins or bounded.

| Stage | Policy |
| --- | --- |
| Frame history | At most 8 entries per camera, pruned at 300 ms relative to the arriving frame. Latest slots advance independently of inference. |
| Phone decode | One-frame leaky decoded queue and `appsink max-buffers=1 drop=true`; encoded VP8 frame dependencies are preserved. |
| Pairing/inference | Closest partner to the slower stream's latest frame, 250 ms pair-age budget, one worker, bounded detection caches. |
| Results | Latest published state; no result queue. Predictions expire 500 ms after an actual observation, including when no new pair arrives. |
| Preview | One-buffer leaky queues before each peer's VP8 encoder. Multiple dashboards still consume additional encoding resources. |
| Telemetry | Replaceable; skipped when disconnected, when replies are pending, or when the next message would exceed the 256 KiB buffered-amount budget. |
| Command replies | FIFO priority over telemetry; at most 32 replies / 64 KiB queued, with a five-second queue-wait deadline. Queue admission is checked before executing another command. Overflow, expiry, or a send error fails the connection explicitly. Commands are never replayed automatically. |

`processing.data_channel` reports per-peer skipped telemetry, send errors, reply failures, replies sent, pending reply count/bytes, and channel buffered bytes. A terminal reply-delivery failure is also recorded as a transport event.

A command can execute before its connection fails, so the frontend warns that the outcome may be unknown and that state should be inspected before retrying. This is bounded best-effort reply delivery, not an exactly-once protocol with client acknowledgement.

## Operating limits

- Pairing uses local receive timestamps, optionally corrected by a measured phone offset. It does not establish synchronized sensor exposure. Network and decode variability can affect moving-hand geometry even when receive-time pairing looks acceptable. Clock probes and motion-based timing estimates do not by themselves prove exposure synchronization. The [evaluation](evaluation.md) quantifies what an unobserved exposure delay costs.
- Metric tracking requires valid camera intrinsics and a calibrated, fixed stereo rig. Moving the phone or the laptop lid, or changing camera modes, invalidates the geometry. See [calibration](calibration.md).
- The supported setup is one person with at most two visible hands. Occlusion, lighting, motion, and calibration quality affect reconstruction. Brief bounded prediction covers gaps but is not a new measurement.
- Core ML can partition execution between Apple accelerators and CPU. Selecting that provider does not prove every operator ran on the GPU or Neural Engine. See [inference](inference.md).
- Synthetic browser integration exercises transport and rendering. It does not establish real-phone HTTPS behavior, physical camera timing, tracking accuracy, or end-to-end latency. See [verification](verification.md) and [tracking diagnostics](tracking-diagnostics.md).
- Native macOS is the documented path for built-in camera capture and Core ML. Full container camera operation requires the documented Linux setup. See [Docker limits](docker.md).
- The liquid-metal effect is procedural rendering with an approximate volume budget, not a physical fluid simulation. Its visual prediction is separate from backend tracking.

## Scope of the single-process design

One local process owns capture, inference, geometry, and transport, with a single processing worker that handles both views sequentially and caches repeated detections. There is no inference job queue. Adding workers would require first measuring the inference bottleneck and defining output ordering; see [runtime ownership](runtime-ownership.md) for the state and lock contract that any such change would have to preserve.
