import test from 'node:test'
import assert from 'node:assert/strict'
import { fitVideo, validHands } from '../src/demoMode.ts'
test('portrait camera is letterboxed without shifting landmarks', () => {
  const r = fitVideo(1000, 500, 720, 1080)
  assert.ok(Math.abs(r.width - 1000 / 3) < 1e-9)
  assert.equal(r.left + r.width / 2, 500)
  assert.equal(r.height, 500)
  assert.equal(r.top, 0)
})
test('landscape camera aligns vertically in portrait container', () => {
  const r = fitVideo(300, 600, 1920, 1080)
  assert.equal(r.width, 300)
  assert.equal(r.left, 0)
  assert.equal(r.top + r.height / 2, 300)
})
test('bad tracking samples are excluded and rendering is bounded to two hands', () => {
  const hand = Array.from({ length: 21 }, () => [0.5, 0.5, 0])
  assert.equal(validHands([[], [[NaN, 0]], hand, hand, hand]).length, 2)
  assert.equal(validHands([hand.map((p, i) => (i === 8 ? [Infinity, 0] : p))]).length, 0)
})
