# Geometry evaluation and evidence boundaries

This project distinguishes geometric reconstruction, model landmark quality, interaction stability and camera-to-display latency. A passing rendering or transport test does not establish any of the other three.

## Reproduce the synthetic geometry experiment

After `make configure`, run:

```bash
make evaluate
# Save raw output with toolchain and machine metadata:
node scripts/evaluate-geometry.mjs /tmp/dualview-geometry.json
```

The executable also runs in CTest with numerical correctness checks. Timing values are reported, not used as CI thresholds. [Recorded machine-readable results](evaluation/synthetic-geometry.json) include the compiler, OpenCV version, build type, seed, machine and timestamp. The checked-in recording is a single local Release run on Apple M3 Pro; it is not a performance guarantee.

The experiment projects 21 known synthetic points into two rectified 640×480 pinhole cameras (550 px focal length). Each scenario contains 200 trials after 10 warmup trials. Pixel noise is independent zero-mean Gaussian noise with 1 px standard deviation where enabled. The pseudorandom seed is 7319; exact noise sequences can vary between C++ standard-library implementations. Reproduction within a toolchain is deterministic apart from durations.

Each trial uses a fresh public `HandTracker`. Errors compare **raw measured** 3D landmarks with the laptop-exposure reference, excluding filtering and prediction. The reported percentiles use nearest rank over accepted landmarks; accepted/tried counts are always shown so rejection cannot silently improve accuracy. All-rejected cases report null metrics. The scenario has no detector, camera image noise, lens distortion, occlusion, skin-tone or lighting variation, or neural-model execution.

| Scenario | Accepted trials | 3D error p50 (mm) | 3D error p95 (mm) | Reprojection p50 (px) |
| --- | --- | --- | --- | --- |
| ideal | 200/200 | 0.000 | 0.000 | 0.000 |
| noise_near | 200/200 | 1.938 | 5.295 | 0.470 |
| noise_middle | 200/200 | 5.385 | 15.163 | 0.470 |
| noise_far | 200/200 | 14.614 | 42.021 | 0.470 |
| short_baseline | 200/200 | 28.835 | 84.417 | 0.470 |
| hidden_delay_40ms | 200/200 | 66.942 | 67.194 | 0.000 |
| hidden_delay_80ms | 200/200 | 150.620 | 151.186 | 0.000 |
| wrong_baseline_10pct | 200/200 | 60.248 | 60.474 | 0.000 |
| vertical_outlier | 0/200 | — | — | — |

The normal baseline is 12 cm. Near/middle/far depths are 0.35/0.60/1.00 m; the short-baseline case uses 6 cm at 1 m. The outlier case displaces one phone landmark vertically by 40 px. Wrong-baseline uses a 10% calibration error. Timing cases simulate motion at 0.3 m/s along the stereo baseline, with an unobserved exposure delay and equal receive timestamps.

## What the results establish

Noise amplification increases with depth and decreasing baseline. The ideal reconstruction error is below 0.01 mm in this generated setup; this verifies numerical geometry, not physical precision.

The 40 ms hidden-delay case produces roughly **67 mm median 3D error while reprojection is nearly zero**. The 80 ms case reaches roughly 151 mm. A 10% baseline error likewise produces about 60 mm error at 0.6 m without a large reprojection residual. These are reproducible counterexamples to treating low residual or receive-time pairing as proof of accurate metric tracking.

Consequently, reprojection checks are consistency checks. The product requires a fixed calibrated rig, and receive timestamps are not exposure timestamps. The system cannot detect all errors lying along epipolar lines from these observations alone. Adding a confidence label or prediction would not solve that observability problem.

`tracker_us_p50/p95/max` cover only the public tracker call, excluding frame capture, model inference, queues, transport and rendering. They include triangulation, matching, checks and result construction. No claim about end-to-end latency follows from these microsecond measurements.

## Physical evaluation protocol — not yet measured

Record device models, camera modes, resolutions, focal settings, rig baseline, calibration files, inference provider/fallback, network, build revision and test duration with every run. Keep raw failures and denominators alongside accepted samples.

| Experiment | Independent reference / method | Report |
| --- | --- | --- |
| Camera/rig geometry | Hold out printed-target views from calibration; use measured rigid distances at multiple depths and orientations. | Held-out residual, distance bias and repeatability by depth. This evaluates rig geometry, not hand-keypoint accuracy. |
| Hand landmark accuracy | A consented evaluation set with independently established 3D hand landmarks and synchronized reference observations. | Per-joint error and p50/p95, detection/reconstruction coverage, errors by depth, occlusion, lighting and motion. Do not use the model's own projections as ground truth. |
| Timing sensitivity | Static and moving target trials, with an independent observation of exposure timing where available. | Static-vs-moving bias and relation to measured delay. Motion-correlation offsets alone are not exposure synchronization evidence. |
| Interaction stability | Fixed hands, controlled motion, two-hand crossing, detector-order swaps and occlusion. | Jitter, identity switches, false pinch starts, loss duration and recovery time; measured and predicted frames separately. |
| End-to-end latency | External high-frame-rate video observing a physical motion/event and the rendered display response in the same recording. | Capture rate, measurement uncertainty, p50/p95/max and number of events. Software receive-to-result age is a separate metric. |
| Sustained load | 1, 2 and 4 dashboards for at least 15 minutes, then a stalled connection and recovery. | CPU, RSS trajectory, event-loop responsiveness, history/queue depths, skipped telemetry, reply failures, frame-age percentiles and reconnect recovery. |

These experiments require physical setup and independent reference data. No physical hand-accuracy, synchronized-exposure or end-to-end-latency claim is made by the current repository evidence. Demo media is still a presentation gap until a real capture is supplied.
