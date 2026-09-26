import test from 'node:test'
import assert from 'node:assert/strict'
import { metricHand, palmCenter, projectionMatches, toCamera } from '../src/spatialMath.ts'
const camera = {
  width: 640,
  height: 480,
  intrinsics: [500, 500, 320, 240],
  distortion: Array(8).fill(0),
  rotation: [
    [0, 0, 1],
    [0, 1, 0],
    [-1, 0, 0],
  ],
  translation: [0.2, 0, 0.1],
}
test('phone uses both stereo rotation and translation in metres', () => {
  const p = toCamera([0.1, 0.2, 0.6], camera)
  assert.ok(Math.abs(p[0] - 0.8) < 1e-10)
  assert.equal(p[1], 0.2)
  assert.equal(p[2], 0)
})
test('a world-space ray hits the same local object point from each camera', () => {
  const center = [0.04, -0.02, 0.65],
    surface = [0.075, 0, 0]
  const worldPoint = center.map((v, i) => v + surface[i])
  const viewCenter = toCamera(center, camera),
    viewPoint = toCamera(worldPoint, camera)
  // Apply R-transpose to camera-space displacement: translations cancel.
  const local = [0, 1, 2].map((i) =>
    camera.rotation.reduce((sum, row, j) => sum + row[i] * (viewPoint[j] - viewCenter[j]), 0),
  )
  local.forEach((v, i) => assert.ok(Math.abs(v - surface[i]) < 1e-9))
})
test('metric hand guard rejects invalid, missing, and behind-camera points', () => {
  const hand = Array.from({ length: 21 }, () => [0.02, 0.01, 0.6])
  assert.equal(metricHand(hand), true)
  assert.deepEqual(palmCenter(hand), [0.02, 0.01, 0.6])
  assert.equal(metricHand(null), false)
  assert.equal(metricHand(hand.map((p, i) => (i === 7 ? [NaN, 0, 0.5] : p))), false)
  assert.equal(metricHand(hand.map(() => [0, 0, -0.5])), false)
})
test('a resolution change disables projection until refreshed', () => {
  assert.equal(projectionMatches(camera, 640, 480), true)
  assert.equal(projectionMatches(camera, 1280, 720), false)
  assert.equal(projectionMatches(undefined, 640, 480), false)
})
