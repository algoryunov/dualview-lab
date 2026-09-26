import test from 'node:test'
import assert from 'node:assert/strict'
import { metalBlockReason } from '../src/metalReadiness.ts'
const fresh = { available: true, age_ms: 20, width: 1280, height: 720 }
const base = {
  laptop: fresh,
  phone: fresh,
  calibration: { status: 'calibrated' },
  hand_tracking: { status: 'tracked', confidence: 0.54, reason: null },
}
test('explains a tracked hand rejected by confidence threshold', () => {
  assert.match(metalBlockReason(base, 10), /54%.*55%/)
})
test('reports reprojection failure instead of asking for a gesture', () => {
  const t = {
    ...base,
    hand_tracking: {
      ...base.hand_tracking,
      status: 'unavailable',
      reason: 'reprojection_residual_too_high',
    },
  }
  assert.match(metalBlockReason(t, 10), /cameras disagree/)
})
test('stale telemetry takes precedence over previous tracking status', () => {
  assert.match(metalBlockReason(base, 700), /No fresh server data/)
})
test('a missing camera and a hand lost in a view get distinct explanations', () => {
  assert.match(metalBlockReason({ ...base, phone: { available: false } }, 10), /Video stream/)
  assert.match(
    metalBlockReason(
      {
        ...base,
        hand_tracking: { status: 'unavailable', reason: 'hand_lost_in_one_or_both_views' },
      },
      10,
    ),
    /Hand lost/,
  )
})
