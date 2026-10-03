// Offline analysis of a per-frame pose capture (DUALVIEW_TRACKING_CAPTURE).
//
// There is no ground truth in a live capture, so this does not rank estimators
// by accuracy. It reports the two quantities that bracket the trade every
// filter makes -- jitter while the hand is still, and lag while it moves -- plus
// the measurement-noise scale needed to tune the Kalman filter.
//
// Usage: node scripts/analyze-capture.mjs logs/dualview-capture.jsonl
import fs from 'node:fs'
import readline from 'node:readline'

const path = process.argv[2]
if (!path) {
  console.error('Usage: node scripts/analyze-capture.mjs <capture.jsonl>')
  process.exit(1)
}

const quantile = (values, q) => {
  if (!values.length) return null
  const sorted = [...values].sort((a, b) => a - b)
  return sorted[Math.min(sorted.length - 1, Math.floor(q * sorted.length))]
}
const mm = (value) => (value == null ? '—' : value.toFixed(2).padStart(8))
const distance = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) * 1000
const centroid = (points) =>
  points.reduce((acc, p) => [acc[0] + p[0] / points.length, acc[1] + p[1] / points.length, acc[2] + p[2] / points.length], [0, 0, 0])

let header = null
const samples = []
const rl = readline.createInterface({ input: fs.createReadStream(path), crlfDelay: Infinity })
for await (const line of rl) {
  if (!line.trim()) continue
  let record
  try {
    record = JSON.parse(line)
  } catch {
    continue
  }
  if (record.event === 'capture_header') header = record
  else if (record.event === 'pose_sample') samples.push(record)
}

if (!samples.length) {
  console.error('No pose_sample records found. Was DUALVIEW_TRACKING_CAPTURE set during the run?')
  process.exit(1)
}

const span = samples.at(-1).t_s - samples[0].t_s
const measured = samples.filter((s) => s.primary?.source === 'measured')
const gaps = samples.slice(1).map((s, i) => (s.t_s - samples[i].t_s) * 1000)

console.log(`capture: ${samples.length} samples over ${span.toFixed(1)}s`)
if (header) {
  console.log(`  published filter: ${header.tracking_filter}  compare: ${header.tracking_filter_compare}`)
  console.log(`  kalman tuning: accel=${header.kalman_accel_sigma} noise_px=${header.kalman_noise_px}`)
  console.log(`  rig: baseline=${header.baseline_cm?.toFixed?.(1) ?? '—'}cm calibrated=${header.calibrated} calib_residual=${header.median_reprojection_error_px?.toFixed?.(2) ?? '—'}px`)
}
console.log(`  frame interval ms: p50=${quantile(gaps, 0.5)?.toFixed(0)} p95=${quantile(gaps, 0.95)?.toFixed(0)}`)
console.log(`  measured ${measured.length}/${samples.length} (${((100 * measured.length) / samples.length).toFixed(0)}%)`)

const reasons = {}
for (const s of samples) reasons[s.reason ?? 'measured'] = (reasons[s.reason ?? 'measured'] ?? 0) + 1
console.log(`  reasons: ${JSON.stringify(reasons)}`)

// Measurement noise scale. Reprojection residual is not landmark noise, but it
// is the only in-band proxy a live capture provides, and it is what the Kalman
// measurement covariance is scaled from.
const residuals = measured.flatMap((s) => s.primary.reprojection_residuals_px ?? [])
if (residuals.length) {
  const p50 = quantile(residuals, 0.5)
  console.log(`\nreprojection residual px: p50=${p50.toFixed(2)} p95=${quantile(residuals, 0.95).toFixed(2)}`)
  if (header?.kalman_noise_px) {
    const ratio = p50 / header.kalman_noise_px
    console.log(`  configured kalman_noise_px=${header.kalman_noise_px} → observed/configured = ${ratio.toFixed(1)}x`)
    if (ratio > 1.5 || ratio < 0.67)
      console.log(`  suggestion: DUALVIEW_TRACKING_KALMAN_NOISE_PX=${p50.toFixed(1)}`)
  }
}

// Segment by palm speed. Still segments expose jitter; moving segments expose
// lag. A filter that wins both is strictly better; usually one trades the other.
//
// Speed is measured on the published (smoothed) pose and then median-filtered:
// per-frame landmark noise alone produces tens of mm/s of apparent motion, so
// classifying on the raw centroid would label a still hand as moving.
const STILL_MM_S = 60
const SPEED_WINDOW = 5
const rawSpeeds = []
for (let i = 1; i < measured.length; i++) {
  const dt = measured[i].t_s - measured[i - 1].t_s
  const current = measured[i].primary.filtered_points_m ?? measured[i].primary.raw_points_m
  const previous = measured[i - 1].primary.filtered_points_m ?? measured[i - 1].primary.raw_points_m
  rawSpeeds.push(dt <= 0 || dt > 0.5 || !current || !previous ? null : distance(centroid(current), centroid(previous)) / dt)
}
const speeds = rawSpeeds.map((value, i) => {
  if (value == null) return null
  const half = SPEED_WINDOW >> 1
  const window = rawSpeeds.slice(Math.max(0, i - half), i + half + 1).filter((v) => v != null)
  return window.length ? quantile(window, 0.5) : value
})

const arms = new Map()
const note = (name, bucket, value) => {
  if (!arms.has(name)) arms.set(name, { still: [], moving: [] })
  arms.get(name)[bucket].push(value)
}

for (let i = 1; i < measured.length; i++) {
  const speed = speeds[i - 1]
  if (speed == null) continue
  const bucket = speed < STILL_MM_S ? 'still' : 'moving'
  const sample = measured[i]
  const raw = sample.primary.raw_points_m
  const previousRaw = measured[i - 1].primary.raw_points_m
  // Still: deviation from the raw measurement is smoothing (good when still).
  // Moving: deviation lags behind the measurement along the direction of travel.
  const candidates = [['published', sample.primary.filtered_points_m], ...(sample.primary.filter_alternates ?? []).map((a) => [a.strategy, a.filtered_points_m])]
  for (const [name, pose] of candidates) {
    if (!pose) continue
    const deviation = distance(centroid(pose), centroid(raw))
    note(name, bucket, deviation)
  }
  note('raw_step', bucket, distance(centroid(raw), centroid(previousRaw)))
}

const still = arms.get('raw_step')?.still.length ?? 0
const moving = arms.get('raw_step')?.moving.length ?? 0
console.log(`\nsegments: still=${still} moving=${moving} (threshold ${STILL_MM_S} mm/s)`)
console.log('\ndeviation of each estimator from the raw measurement, mm (centroid):')
console.log('  estimator    | still p50 | still p95 | moving p50 | moving p95')
console.log('  ' + '-'.repeat(62))
for (const [name, buckets] of arms) {
  if (name === 'raw_step') continue
  console.log(
    `  ${name.padEnd(12)} |${mm(quantile(buckets.still, 0.5))}   |${mm(quantile(buckets.still, 0.95))}   |${mm(quantile(buckets.moving, 0.5))}    |${mm(quantile(buckets.moving, 0.95))}`,
  )
}
console.log('\nWhile still, a larger deviation means more smoothing of noise.')
console.log('While moving, a larger deviation is mostly lag behind the hand.')
console.log('This is a trade-off readout, not an accuracy ranking: the capture has no ground truth.')
