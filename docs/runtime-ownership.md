# Runtime ownership and concurrency contract

`Runtime` is the composition root and the serialized owner of live application state. It is deliberately one local process with one processing worker. Capture and library-managed transport threads deliver frames independently. Adding workers would require measuring the inference bottleneck and defining output ordering first.

## Sources of truth

| State | Owner / access | Rule |
| --- | --- | --- |
| Startup configuration | Immutable `Config` | Normalize paths before starting threads; never mutate configuration to represent a session. |
| Latest frames, histories, input generations | `FrameStore`, internal mutex | Capture/preview never take the runtime state mutex. Published image buffers are immutable; readers retain OpenCV references. |
| Tracking history / last result | `HandTracker` / `TrackingResult`, runtime state mutex | The worker copies history, computes privately, then commits a matching revision. Commands invalidate history under the same mutex. |
| Object and gesture state | `InteractionController`, runtime state mutex | Commands and accepted tracking updates are ordered by this lock. Stop retains the transform; reset restores the transform and retains model selection. |
| Calibration observations | `CalibrationSession`, runtime state mutex | Expensive processing operates on a private copy; cancelled copies cannot commit. Published geometry is separate. |
| Active timing correction | Runtime state mutex plus `WorkRevisions` | Explicit reset means zero; phone replacement restores the startup value. Both invalidate processing work. |
| Timing diagnostic session | `DiagnosticSession`, runtime state mutex | Phase, samples, estimate validity, counts and generation have one owner. Correlation runs on a copied `Work`; late completion and pre-session samples are rejected. |
| Metal log | `MetalDiagnostics`, its own writer mutex | Runtime captures typed context and releases its mutex before calling the logger. Throttling and rotation are serialized. Atomic diagnostic status does not wait for disk I/O. |
| Protocol metadata | `metadata_`, runtime state mutex | Calibration progress and diagnostics only. There is no stored JSON replica of hand or interaction state. |
| Inference pipeline / detection cache | Processing worker | Never used by commands. Constructors finish before processing starts. Destruction explicitly joins both application threads. |

`snapshot()` copies metadata, typed tracking/interaction state, diagnostic status, geometry and current frame references under the state lock. It releases the lock before pose serialization and projection metadata construction. Frame counters and metal-log status are observational counters sampled independently; the response is not a transaction spanning those counters. Live input frames can be newer than the last processing result. Frame-to-result age and inference-input sequences in diagnostic logs make that distinction visible.

## Lock ordering

When nested, the order is **persistence mutex -> runtime state mutex -> FrameStore mutex**. FrameStore never calls back into Runtime. Metal logging has no nested runtime lock. No model execution, ChArUco detection/solve, triangulation, timing correlation or filesystem write may run under the state/frame locks.

The persistence mutex orders calibration start/cancel, phone replacement, and completed geometry saves. Computation does not hold it. After computation, the worker acquires persistence, validates the revision under the state lock, releases the state lock, saves YAML, and only then commits geometry/progress. A failed save leaves the old published geometry and reports failure. A slow save must not publish an old pose as a fresh measurement.

Cancellation is not interruption of a filesystem operation: if saving has already acquired persistence, cancellation waits for that save and publication. This is an explicit ordering guarantee, not a bounded cancellation-time guarantee. Frame input and previews continue while saving.

## Revision checks and command ordering

`WorkRevisions::Token` contains phone, timing and calibration revisions. Token capture, transitions, comparison and publication all take the state mutex. Comparison alone is insufficient: the lock must remain held until the corresponding result is committed.

| Transition | Invalidated work |
| --- | --- |
| Phone replacement/disconnect | Phone detections, timing assumptions and calibration computations; a session using the phone is cancelled. |
| Apply/reset timing | Computations using the old correction and previous tracking history. |
| Start/cancel calibration | Computations carrying the previous calibration session/geometry assumptions. |
| Start/cancel diagnostics | Diagnostic samples/completion for that session; ordinary tracking may continue. |
| Select/reset object | Applied synchronously under the state mutex. The next accepted tracking result can begin a new grab, including a frame already being processed. |

The separate FrameStore phone generation protects the image-copy interval, before a frame reaches processing. The processing phone revision protects inference after an input snapshot. These are distinct invalidation boundaries.

`runtime_protocol.cpp` validates wire requests into `RuntimeCommand`, calls `Runtime::execute`, and serializes `CommandReply`. JSON names are not the runtime command-dispatch mechanism. Responses retain the existing frontend schema. Malformed input is rejected before mutation; command retries are never automatic.

## Evidence and remaining limits

- `work_revisions_test` uses latches to complete old work after each invalidating transition; no timing sleeps are needed.
- `diagnostic_session_test` checks late/duplicate completion, replacement, expiry and rejection of pre-session samples with an explicit clock.
- `runtime_concurrency_test` concurrently publishes frames, changes models/timing, resets/disconnects and reads snapshots; then exercises repeated calibration cancellation through public APIs.
- These tests do not exhaust all schedules or force every filesystem failure interleaving. See the ThreadSanitizer record in [verification](verification.md).
- Driver-level `VideoCapture::read`, model execution and filesystem operations are not forcibly interruptible. Shutdown joins safely but does not have a measured worst-case time on arbitrary drivers.
- Metadata aggregation and log-record construction still live in Runtime. Further extraction should follow a concrete change or measured contention rather than introducing another general scheduling layer.
