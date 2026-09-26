import test from 'node:test'
import assert from 'node:assert/strict'
import { distribution, summarize } from '../analyze-tracking.mjs'
test('missing frames do not inflate synchronization statistics', () => {
  const result = summarize([{ event: 'tracking_sample', timestamp_ms: 1000, timing_error_ms: 1e12,
    phone: { sample_age_ms: null }, laptop: { sample_age_ms: 40 } }])
  assert.equal(result.fresh_pairs.pair_difference_ms.samples, 0)
  assert.equal(result.missing_or_stale_samples, 1)
})
test('controlled phases and nonfinite values stay separate', () => {
  const result = summarize([{ event: 'tracking_sample', timestamp_ms: 1000, timing_error_ms: 10,
    phone: { sample_age_ms: 50 }, laptop: { sample_age_ms: 50 }, processing: { diagnostic_phase: 'stationary' } }])
  assert.equal(result.controlled_tests.stationary.logged_samples, 1)
  assert.equal(result.controlled_tests.moving.logged_samples, 0)
  assert.equal(distribution([null, NaN, 4]).p95, 4)
})

test('movement trials are separated by applied offset', () => {
  const make = offset => ({ event: 'tracking_sample', timestamp_ms: 1000, timing_error_ms: 5,
    laptop: {sample_age_ms: 30}, phone: {sample_age_ms: 30},
    processing: {diagnostic_phase: 'moving', phone_time_offset_ms: offset} })
  const result = summarize([make(0), make(90), make(90)])
  assert.equal(result.compensated_movement['0'].logged_samples, 1)
  assert.equal(result.compensated_movement['90'].logged_samples, 2)
})
