# Verification

## Automated checks

Run `make build`, `make test`, and `make lint`. Native model tests require the local weights installed by `make backend-install`. The Core ML test must run outside a filesystem sandbox that blocks Apple's compiler cache.

Run the browser integration after building:

```bash
cd frontend && npx playwright install chromium && cd ..
make integration-test
# Also exercise the actual Apple inference provider:
TEST_PROVIDER=coreml make integration-test
```

The runner starts an isolated native server with synthetic camera input and a temporary calibration path, runs with QR shown and hidden, then stops the server and removes its temporary data. It does not modify local calibration or require the normal development server. Inference is disabled unless `TEST_PROVIDER` selects a provider. `CHROME_PATH` can select an existing Chromium executable.

Checks cover native signaling/DataChannel readiness, incoming VP8 and actual returned preview pixels, commands, tabs, rendering, phone stop/start and replacement, dashboard reconnect, and pairing-link expiry. Security checks reject foreign-origin mutations, invalid tokens, and superseded tokens. These checks use loopback HTTP; use the hardware checklist for LAN HTTPS and physical capture.

Run `make format-check` to verify C++ and frontend formatting (requires `clang-format`). Use `make format` to apply it.

## Geometry and concurrency checks

Run `make evaluate` for the synthetic geometry report. See [evaluation methodology and results](evaluation.md) for noise/baseline/depth sensitivity and hidden timing/calibration errors. Timing aggregates cover the tracker call only. Numerical properties are checked by CTest; hardware-dependent durations are not pass/fail thresholds.

The native suite now includes latch-controlled revision invalidation, diagnostic-session replacement/expiry, and concurrent public Runtime API calls. For a separate instrumented build:

```bash
cmake -S backend -B backend/build-tsan -DDUALVIEW_TSAN=ON \
  -DDUALVIEW_BUILD_SERVER=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build backend/build-tsan --target runtime_concurrency_test \
  work_revisions_test diagnostic_session_test data_outbox_test --parallel
ctest --test-dir backend/build-tsan \
  -R '^(runtime_concurrency|work_revisions|diagnostic_session|data_outbox)$' --output-on-failure
```

Local record, 2026-09-26: all 19 ordinary native tests passed, including CPU/Core ML. The public Runtime concurrency test also passed 20 consecutive runs. The 40 frontend tests, seven script tests, builds, lint, formatting and repository checks passed; browser WebRTC integration passed with QR shown and hidden using installed Chrome and inference disabled. The isolated `work_revisions` and `data_outbox` targets passed ThreadSanitizer. Full Runtime and diagnostic tests did **not** complete under TSan: the local OpenCV 4.14 / TBB 12.19 stack crashes in `tbb::detail::r1::__TBB_InitOnce::~__TBB_InitOnce()` during process exit. A standalone program that only constructs `cv::Mat` reproduces the same crash without Runtime. This is a sanitizer-environment limitation, not evidence of a clean full-runtime TSan run. No suppression or early-exit workaround is used.

The minimal reproducer is:

```cpp
#include <opencv2/core.hpp>
int main() {
  cv::Mat image = cv::Mat::zeros(48, 64, CV_8UC3);
  return image.empty();
}
```

Compile/link it with `-fsanitize=thread` and the same OpenCV installation. Full sanitizer coverage remains pending a compatible instrumented dependency stack. The standalone core targets intentionally do not link OpenCV/ONNX Runtime.

## Real hardware checklist

- Load HTTPS from a phone on the same LAN without certificate errors.
- Connect and disconnect the phone; verify the dashboard recovers and stale tracking disables interaction.
- Calibrate both cameras and the fixed stereo pair. Check reported error and physical baseline.
- Move one palm in both views, then add a second palm. Check material position and stretching.
- Check model switching, pinch movement, two-hand scaling, and rotation in Experiments.
- Verify Light Trails and Energy, including missing-hand behavior.
- Check diagnostics for the intended inference provider and any errors.
- Restart and verify saved calibration and env settings persist.

## Existing local profiles

Import JSON calibration once, preserving original files:

```bash
node scripts/import-calibration.mjs \
  data/calibration/intrinsics/macbook-camera-0.json \
  data/calibration/intrinsics/galaxy-s21-front.json \
  data/calibration/stereo.json
```

The importer refuses to overwrite native calibration. Select profiles matching the camera modes used for the stereo result. Imported quality is preserved, including a degraded result; importing does not improve calibration accuracy.

## Local verification record

On 2026-09-23, the C++ and frontend builds, 32 frontend tests, seven script tests, all 12 native tests (including CPU and Core ML model execution), lint, formatting, and repository checks passed. The npm dependency audit reported no known vulnerabilities at the time of the check.

Browser integration passed with QR shown and hidden using installed Chrome, synthetic cameras, and inference disabled. This covered WebRTC signaling and video forwarding, navigation, rendering, reconnects, pairing rejection, and rendered metal samples persisted through the DataChannel. Native model tests separately exercised CPU and Core ML; this browser run does not establish end-to-end inference accuracy.

Physical phone capture, live hand accuracy, and measured calibration quality still require the hardware checklist above. The GitHub Actions workflow is included but has not yet run on GitHub.

## Motion and depth regression

Frontend motion tests compare 30, 60, and 144 Hz responses, metric stretch along all three axes, and landmark/shader projection alignment. The native tracking test triangulates two synthetic hands at different depths, checks confidence for the second hand, preserves primary-hand identity when detector ordering changes, and clears the second hand when it disappears.

For the synthetic WebGL fixture, start `npm --prefix frontend run dev -- --port 5175 --strictPort`, then run `node frontend/tests/spatial-browser.mjs`. `TEST_SPATIAL_ORIGIN` overrides that local origin; `CHROME_PATH` selects a Chromium executable. The test checks depth stretching, near/far motion, tracking loss/recovery, and shader errors, and writes screenshots to the OS temporary directory for visual review. It does not measure real camera-to-display latency.

The phone-video regression also verifies HD capture, outbound WebRTC resolution, matching decoded preview dimensions, RTX/NACK negotiation for every peer, and the quality selection surviving a page reload. Native runtime tests restore camera profiles with and without a stereo solution, check resolution compatibility, and retain profiles after disconnect. See [video quality diagnostics](video-quality.md) to investigate camera noise, network adaptation, or preview encoding separately.

## Tracking loss diagnostics

Native runs write JSON lines to `logs/dualview-native.jsonl` by default. Set `DUALVIEW_TRACKING_LOG` to another path or `-` for stderr. Local files rotate at 2 MB, retaining three backups. Docker Compose routes events to its bounded container log (`docker compose logs -f backend`).

Each sample includes status/reason, cumulative loss/recovery counts for the process, detected-hand counts and accepted detection confidences per camera, current and inference-input frame ages, pair timing error, candidate reconstruction residual when available, processing duration/provider, pose source/hold age, and interaction state. Interaction includes both pinch distances, object position/scale, and blocking reason. Images and complete landmark arrays are not logged. Normal sampling is once per second; state changes can emit at most ten samples per second. Loss counters track measured primary tracking, including transitions to predicted or held poses. Counts are state transitions, not unique failed frames; a camera that cannot detect a hand contributes no accepted confidence score.

## Metal diagnostics

The current renderer is `swept-fluid-v3`: five viscous control nodes form a continuous curved body, sampled into eight tapered segments. Thickness changes along the whole body under an approximate volume budget, with traveling bulges, transverse drag, and a slower middle response. Curve interpolation runs once per frame on the CPU. This is a procedural visual model, not a Navier–Stokes solver or exact physical volume conservation. Unit tests check its volume proxy and positive continuous radii.

Local Chromium measurement on 2026-09-22: 180 animation intervals with three synthetic liquid views had an 8.3 ms median and 9.3 ms 95th percentile. The frontend suite passed 27 tests and the native suite passed eight tests excluding Core ML. This rendering measurement excludes physical camera/inference latency and is not a frame-rate guarantee on other devices.

The stage shows **Metal log: Recording** only after a diagnostic command succeeds; failed commands are displayed. Setup diagnostics expose `metal_log_file`, `metal_log_samples`, and any write error. If the file is absent, reload the dashboard after rebuilding/restarting and check this status on the main metal tab. Model-only sessions do not emit metal render samples. Each new sample carries `renderer`; the legacy `neck` field now describes the smoothed one/two-hand blend indicator, not a separate connecting tube.

`DUALVIEW_METAL_LOG` defaults to `logs/dualview-metal.jsonl`. The main metal stage sends its actual render state over the control DataChannel at up to 10 Hz: one/two-hand or blocked/auto phase, palm distance, center in metres, stretch axis, separation, neck blend, opacity, blob positions/radii, spring velocities, and frame duration. Blob positions and velocities use local material units (one unit is 0.075 m). Each record has browser monotonic time and server wall-clock time; the attached backend tracking/interaction snapshot is taken on receipt and may be newer than the rendered sample. The file rotates at 2 MB with three backups. It is written only while the main metal stage is visible and connected, not while viewing the model experiment. Model gestures are recorded in the native tracking log.

After rebuilding and restarting, reproduce the close-palm transition and inspect `tail -f logs/dualview-metal.jsonl`. The backend bridges missing detections for up to 500 ms with damped velocity prediction capped at 3 cm, provided both frames remain fresh, paired, and calibrated. Only an existing grab may translate during prediction; rotation, scale changes, and new gestures are suspended. Predicted points are labeled `predicted`, keep raw points empty, and never renew the observation time. The confidence threshold remains 0.55. The metal renderer separately bridges brief detection gaps for up to 350 ms from its last received measured pose, with damped velocity prediction capped at 2.5 cm. Held poses do not renew that window; stale streams or invalid calibration clear it. This visual prediction does not feed gesture commands or establish new measurements. Near-coincident palms retain the last stable axis. The metal merges at an 18 cm palm-center distance, starts compressing below 16 cm, and reaches full compression at 7 cm. Its center lifts by up to approximately 5 cm along camera-up; the volume proxy is 50% larger than v2. These are visual interaction settings, not a physical fluid simulation.

Docker Compose stores the separate metal file in `/data/logs/dualview-metal.jsonl` in the persistent `diagnostics` volume. Read it with `docker compose exec backend tail -f /data/logs/dualview-metal.jsonl`.

Use `tail -f logs/dualview-native.jsonl` while reproducing the issue after restarting the rebuilt server. Compare `hand_lost_in_one_or_both_views`, `frame_pair_out_of_sync`, `stereo_geometry_invalid`, `reprojection_residual_too_high`, `stale_frames`, and `processing_error`. Current frame age can be small even when inference-input age is large: that indicates the worker is processing older input. Logging provides evidence; it does not itself fix tracking loss or relax geometry checks.
