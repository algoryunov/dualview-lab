import test from 'node:test'
import assert from 'node:assert/strict'
import { MetalHandTracker } from '../src/metalTracking.ts'
const points = (x) => Array.from({ length: 21 }, () => [x, 0, 0.5])
const sample = (x = 0, second = true) => ({
  hand_tracking: {
    status: 'tracked',
    confidence: 0.95,
    secondary_confidence: 0.95,
    source: 'measured',
    secondary_source: second ? 'measured' : 'missing',
    filtered_points_m: points(x),
    secondary_filtered_points_m: second ? points(0.2) : null,
  },
})
const lost = {
  hand_tracking: {
    status: 'unavailable',
    confidence: 0,
    source: 'missing',
    secondary_source: 'missing',
  },
}

test('short gaps preserve both hands but repeated missing telemetry cannot renew them', () => {
  const tracker = new MetalHandTracker()
  assert.equal(tracker.update(sample(), 0, 0, true).length, 2)
  assert.equal(tracker.update(sample(0, false), 100, 100, true).length, 2)
  assert.equal(tracker.update(lost, 200, 200, true).length, 2)
  assert.equal(tracker.update(lost, 351, 351, true).length, 1)
  assert.equal(tracker.update(lost, 451, 451, true).length, 0)
})
test('held backend poses cannot extend visual prediction', () => {
  const tracker = new MetalHandTracker()
  tracker.update(sample(), 0, 0, true)
  const held = sample()
  held.hand_tracking.source = held.hand_tracking.secondary_source = 'held'
  assert.equal(tracker.update(held, 200, 200, true).length, 2)
  assert.equal(tracker.predicted, true)
  assert.equal(tracker.update(held, 351, 351, true).length, 0)
})
test('motion prediction is bounded and does not modify the measurements', () => {
  const tracker = new MetalHandTracker()
  tracker.update(sample(), 0, 0, true)
  const moving = sample(0.04)
  tracker.update(moving, 100, 100, true)
  const predicted = tracker.update(lost, 300, 300, true)
  assert.ok(predicted[0][0][0] > 0.04 && predicted[0][0][0] <= 0.065)
  assert.equal(moving.hand_tracking.filtered_points_m[0][0], 0.04)
  const jump = tracker.update(sample(0.8), 310, 310, true)
  assert.equal(jump[0][0][0], 0.8)
})
test('camera loss, calibration loss, and expired telemetry clear predictions immediately', () => {
  const tracker = new MetalHandTracker()
  tracker.update(sample(), 0, 0, true)
  assert.deepEqual(tracker.update(sample(), 20, 20, false), [])
  assert.deepEqual(tracker.update(lost, 30, 30, true), [])
  tracker.update(sample(), 50, 50, true)
  assert.deepEqual(tracker.update(sample(), 50, 701, true), [])
})

test('backend predictions cannot be reused as fresh metal measurements', () => {
  const tracker = new MetalHandTracker()
  tracker.update(sample(), 0, 0, true)
  const predicted = sample()
  predicted.hand_tracking.source = predicted.hand_tracking.secondary_source = 'predicted'
  assert.equal(tracker.update(predicted, 200, 200, true).length, 2)
  assert.equal(tracker.update(predicted, 351, 351, true).length, 0)
})
