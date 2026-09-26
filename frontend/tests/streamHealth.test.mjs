import test from 'node:test'
import assert from 'node:assert/strict'
import { hasFreshFrame, streamDelayMessage } from '../src/streamHealth.ts'
const frame = { available: false, width: 1280, height: 720, age_ms: 25 }
test('fresh raw frames remain usable with legacy false availability flag', () => {
  assert.equal(hasFreshFrame(frame), true)
})
test('old resolution metadata does not prove a live stream', () => {
  assert.equal(hasFreshFrame({ ...frame, available: true, age_ms: 10553 }), false)
  assert.equal(hasFreshFrame({ ...frame, age_ms: null }), false)
  assert.equal(hasFreshFrame({ ...frame, age_ms: NaN }), false)
})
test('diagnostic identifies the stalled camera and frame age', () => {
  assert.equal(
    streamDelayMessage('Phone', { ...frame, age_ms: 10553 }),
    'Phone: last frame 10.6 s ago',
  )
})
