import test from 'node:test'
import assert from 'node:assert/strict'
import { fluidRadii, sweptVolume, fluidNodes } from '../src/fluidShape.ts'

test('stretching redistributes a bounded volume throughout the liquid body', () => {
  for (const span of [0, 0.01, 0.4, 1, 3, 8, 20]) {
    for (const time of [0, 0.5, 2, 10]) {
      const lengths = Array(4).fill(span / 4)
      const radii = fluidRadii(lengths, time, 0.8)
      assert.ok(radii.every((r) => Number.isFinite(r) && r > 0))
      assert.ok(Math.abs(sweptVolume(lengths, radii) - 3.6) < 0.0001)
    }
  }
  const short = fluidRadii([0.1, 0.1, 0.1, 0.1], 0, 0)
  const long = fluidRadii([1, 1, 1, 1], 0, 0)
  assert.ok(
    long.every((radius, i) => radius < short[i]),
    'The ends must also thin, not remain rigid spheres',
  )
})

test('fluid curve has continuous positive thickness and follows its control points', () => {
  for (let time = 0; time < 8; time += 0.1) {
    const radii = fluidRadii([0.5, 0.5, 0.5, 0.5], time, 1)
    assert.ok(
      radii[0] > radii[2] * 2 && radii[4] > radii[2] * 2,
      'Both palm droplets must remain wider than the bridge',
    )
    const points = radii.map((r, i) => [i * 0.5, Math.sin(i) * 0.1, 0, r])
    const nodes = fluidNodes(points)
    points.forEach((point, i) =>
      point.forEach((v, j) => assert.ok(Math.abs(nodes[i * 2][j] - v) < 1e-10)),
    )
    assert.ok(nodes.every((p) => p.every(Number.isFinite) && p[3] > 0))
    const next = fluidRadii([0.5, 0.5, 0.5, 0.5], time + 1 / 60, 1)
    assert.ok(next.every((r, i) => Math.abs(r - radii[i]) < 0.01))
  }
})

test('compressed liquid keeps a broad center instead of growing a thin neck', () => {
  for (const time of [0, 1, 3, 7]) {
    const lengths = [0.3, 0.3, 0.3, 0.3]
    const radii = fluidRadii(lengths, time, 0, 1)
    assert.ok(radii[2] > radii[0] && radii[2] > radii[4])
    assert.ok(Math.abs(sweptVolume(lengths, radii) - 3.6) < 0.0001)
  }
})
