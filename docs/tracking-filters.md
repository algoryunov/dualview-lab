# Landmark filtering and the estimator comparison

Triangulated landmarks are noisy, and the noise is not isotropic: stereo lateral uncertainty scales as `Z/f`, depth uncertainty as `Z²/(f·B)`. Three estimators are implemented. Only one is on by default, and only that one has been exercised on physical hardware.

| `DUALVIEW_TRACKING_FILTER` | Estimator | Status |
| --- | --- | --- |
| `alpha` (default) | Position-only blend: `filtered = .35·prior + .65·measurement` | Supported path |
| `alpha_beta` | The same blend with its velocity feedforward connected | Experimental |
| `kalman` | Per-axis constant-velocity Kalman filter with measurement noise derived from stereo geometry | Experimental |

Selecting an unknown name fails at startup rather than silently falling back.

## Why there are three

The original filter computes a velocity estimate but uses it **only** for extrapolation during a detection gap — it is never fed back into the position estimate. That makes it an α filter, not the α-β filter its structure suggests. The consequence is lag during motion, which the synthetic comparison below quantifies: at 1.5 Hz the published pose is *worse than not filtering at all*, because the lag it introduces exceeds the noise it removes.

`alpha_beta` connects that feedforward and changes nothing else. `kalman` additionally replaces the fixed gains with gains derived from a motion model and a geometry-derived measurement covariance.

All three reset on a discontinuity: a jump past the 12 cm continuity gate is treated as a new track, so none of them smooth across an identity change.

## Synthetic comparison

Reproduce with:

```bash
cmake --build backend/build --target filter_ab
./backend/build/filter_ab --accel=4 --beta=0.35
```

Median 3D error in millimetres, 150 steps at 30 Hz, σ = 1 px landmark noise. Every arm consumes an identical measurement stream: a persistent `HandTracker` produces `raw` and the production `filtered`, and the other estimators are fed the same `raw`.

| Scenario | raw (no filter) | `alpha` | `alpha_beta` | `kalman` |
| --- | --- | --- | --- | --- |
| `static_near` (0.35 m) | 1.93 | **1.31** | 1.44 | 1.72 |
| `static_mid` (0.60 m) | 5.36 | **3.64** | 3.99 | 4.26 |
| `static_far` (1.00 m) | 14.58 | 9.85 | 10.88 | **9.59** |
| `static_short_baseline` (6 cm) | 28.81 | 19.65 | 21.64 | **16.31** |
| `lateral_slow` (0.5 Hz) | 5.38 | 5.72 | **4.11** | 4.28 |
| `lateral_fast` (1.5 Hz) | 5.39 | 12.79 | 8.87 | **4.34** |
| `depth_slow` (0.5 Hz) | 5.24 | 4.73 | **3.99** | 4.20 |
| `depth_fast` (1.5 Hz) | 5.10 | 10.57 | 6.67 | **5.05** |
| `dropout_every_4` | 5.31 | 7.01 | **4.82** | 5.11 |
| `hidden_delay_40ms` | 43.77 | **43.91** | 44.59 | 44.35 |

What this establishes:

- The default filter wins when the hand is **still and close**, which is the regime it was tuned in.
- It loses badly once the hand **moves**, and the velocity feedforward recovers most of that gap.
- The Kalman filter's advantage concentrates where **depth noise dominates** — far range, short baseline, fast motion. An ablation replacing the anisotropic measurement noise with an isotropic one moves `static_short_baseline` from 14.33 mm to 23.85 mm, confirming the geometry-derived covariance is what earns that win. The same ablation *improves* `depth_fast` (9.42 → 4.68 mm), because distrusting depth also means lagging when depth genuinely changes. It is a trade, not a free win.
- **No estimator addresses `hidden_delay_40ms`.** An unobserved exposure delay is a bias, not zero-mean noise. The Kalman filter tracks it with shrinking covariance, meaning growing confidence in a wrong answer. See [evaluation](evaluation.md).

Tuning sensitivity is real. At `--accel=0.5` the Kalman filter reaches 38 mm on `depth_fast`, four times worse than the default filter. The shipped default of 4 m/s² is a synthetic compromise, not a measured one.

## Comparison mode

`DUALVIEW_TRACKING_FILTER_COMPARE=true` runs the estimators that are *not* publishing alongside the one that is. Their poses never reach interaction, gestures, or calibration; they are recorded so two estimators can be compared on the same live frames.

Telemetry gains `filter_alternates` per hand — each entry carries the strategy name, its filtered landmarks, and `rms_delta_mm` against the published pose. The structured tracking log gains a `filter_comparison` object with the same divergences.

Shadow estimators are recorded on **measured frames only**. Advancing their state during a prediction gap would double-integrate on recovery, so a gap is skipped rather than estimated.

## Capturing real data

The ordinary tracking log (`DUALVIEW_TRACKING_LOG`) is a status-change sampler: it writes at most once per second, carries no landmarks, and cannot show what a filter does between samples. It cannot settle a filter comparison.

`DUALVIEW_TRACKING_CAPTURE` records every processed pair instead — raw and filtered landmarks, residuals, and every shadow estimator's pose:

```bash
DUALVIEW_TRACKING_FILTER_COMPARE=true \
DUALVIEW_TRACKING_CAPTURE=logs/dualview-capture.jsonl \
DUALVIEW_TRACKING_CAPTURE_SECONDS=120 make dev

node scripts/analyze-capture.mjs logs/dualview-capture.jsonl
```

Recording is opt-in, stops after its second limit, and writes outside every runtime lock. Landmarks are rounded to 10 µm.

A useful session alternates **deliberate hold-still segments** with **deliberate fast moves**. The analyser classifies frames by palm speed and reports each estimator's deviation from the raw measurement in each regime: while still that deviation is noise smoothing, while moving it is mostly lag. Those two numbers bracket the trade-off without needing ground truth — which a live capture cannot provide, so the output is not an accuracy ranking.

The analyser also reports the median reprojection residual and compares it against `DUALVIEW_TRACKING_KALMAN_NOISE_PX`, suggesting a value when the configured one is off by more than 1.5x.

## Limits

These numbers come from synthetic geometry with independent Gaussian pixel noise. Real landmark noise is correlated across landmarks and has heavier tails, which changes the measurement covariance and therefore the tuning. No estimator here has been validated against physical ground truth, and the [evaluation protocol](evaluation.md) that would establish one has not been run.

Choosing anything other than `alpha` changes what the interaction layer consumes. Treat the experimental estimators as measurement instruments until a physical capture justifies promoting one.
