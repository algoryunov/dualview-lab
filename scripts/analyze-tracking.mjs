import fs from 'node:fs'
import path from 'node:path'
import { pathToFileURL } from 'node:url'

export function distribution(values) {
  values = values.filter(v => typeof v === 'number' && Number.isFinite(v)).sort((a, b) => a - b)
  if (!values.length) return { samples: 0 }
  return { samples: values.length, ...Object.fromEntries(
    [['p50', .5], ['p95', .95], ['max', 1]].map(([key, q]) =>
      [key, Number(values[Math.floor((values.length - 1) * q)].toFixed(2))])) }
}
export function summarize(rows) {
  const samples = rows.filter(r => r.event === 'tracking_sample').sort((a, b) => a.timestamp_ms - b.timestamp_ms)
  const fresh = samples.filter(r => ['laptop', 'phone'].every(camera =>
    typeof (r[camera]?.pair_sample_age_ms ?? r[camera]?.sample_age_ms) === 'number' && (r[camera]?.pair_sample_age_ms ?? r[camera]?.sample_age_ms) < (r.processing?.pair_max_age_ms ?? 650)))
  const group = items => ({
    logged_samples: items.length,
    reasons: items.reduce((counts, r) => { const key = r.reason ?? 'measured'; counts[key] = (counts[key] ?? 0) + 1; return counts }, {}),
    pair_difference_ms: distribution(items.map(r => r.timing_error_ms)),
    geometry_error_px: distribution(items.map(r => r.candidate_residual_px)),
    processing_ms: distribution(items.map(r => r.processing?.hand_inference_duration_ms)),
    phone_sample_age_ms: distribution(items.map(r => r.phone?.sample_age_ms)),
  })
  return {
    note: 'Event samples, not frame success percentages. Receive-time difference is not capture synchronization.',
    range_utc: samples.length ? [samples[0], samples.at(-1)].map(r => new Date(r.timestamp_ms).toISOString()) : [],
    all_logged_samples: samples.length,
    missing_or_stale_samples: samples.length - fresh.length,
    fresh_pairs: group(fresh),
    controlled_tests: Object.fromEntries(['stationary', 'moving'].map(phase =>
      [phase, group(fresh.filter(r => r.processing?.diagnostic_phase === phase))])),
    timing_estimates: rows.filter(r => r.event === 'timing_estimate').map(r => ({ timestamp_ms: r.timestamp_ms, ...r.result })),
    compensated_movement: Object.fromEntries([...new Set(fresh.filter(r => r.processing?.diagnostic_phase === 'moving').map(r => r.processing?.phone_time_offset_ms ?? 0))].map(offset =>
      [String(offset), group(fresh.filter(r => r.processing?.diagnostic_phase === 'moving' && (r.processing?.phone_time_offset_ms ?? 0) === offset))])),
    connection_events: rows.filter(r => r.event === 'phone_connection').map(r => ({ timestamp_ms: r.timestamp_ms, reason: r.reason })),
  }
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const args = process.argv.slice(2)
  const outputIndex = args.indexOf('--output')
  let output
  if (outputIndex >= 0) {
    output = args[outputIndex + 1]
    if (!output) throw new Error('--output requires a filename')
    args.splice(outputIndex, 2)
  }
  const paths = args.length ? args : fs.readdirSync('logs').filter(p => /^dualview-native\.jsonl(?:\.[123])?$/.test(p)).map(p => path.join('logs', p))
  if (!paths.length) throw new Error('No diagnostic logs found')
  const rows = []
  let malformed = 0
  for (const file of paths) for (const line of fs.readFileSync(file, 'utf8').split('\n').filter(Boolean)) {
    try { const row = JSON.parse(line); if (row && typeof row === 'object') rows.push(row) } catch { malformed++ }
  }
  const report = JSON.stringify({ ...summarize(rows), malformed_lines: malformed }, null, 2) + '\n'
  if (output) fs.writeFileSync(output, report)
  else process.stdout.write(report)
}
