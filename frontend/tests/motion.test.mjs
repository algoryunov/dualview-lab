import test from 'node:test'
import assert from 'node:assert/strict'
import { followAmount, springStep, metalHalfSpan } from '../src/motion.ts'

test('model follows continuously with equal response at 30, 60 and 144 Hz', () => {
  for (const fps of [30, 60, 144]) {
    let value = 0
    for (let i = 0; i < fps; i++) {
      const next = value + (1 - value) * followAmount(1 / fps)
      assert.ok(next >= value && next <= 1)
      value = next
    }
    assert.ok(Math.abs(value - (1 - Math.exp(-22))) < 1e-12)
  }
})

test('liquid spring converges without overshoot at different frame rates', () => {
  const end = []
  for (const fps of [30, 60, 144]) {
    let position = 0,
      velocity = 0
    for (let i = 0; i < fps; i++) {
      ;[position, velocity] = springStep(position, velocity, 3, 1 / fps)
      assert.ok(position >= 0 && position <= 3)
    }
    end.push(position)
  }
  assert.ok(Math.max(...end) - Math.min(...end) < 1e-12)
  assert.ok(end[0] > 2.999)
})

test('two-hand stretch preserves metric span for lateral, depth, and diagonal pulls', () => {
  for (const displacement of [
    [0.3, 0, 0],
    [0, 0, 0.3],
    [0.2, 0.1, 0.3],
  ]) {
    const distance = Math.hypot(...displacement)
    const half = metalHalfSpan(distance, 0.075)
    displacement.forEach((component) => {
      const rendered = (component / distance) * half * 2 * 0.075
      assert.ok(Math.abs(rendered - component) < 1e-12)
    })
  }
})

test('liquid merges before palms touch and opens continuously', async () => {
  const { liquidHalfSpan } = await import('../src/motion.ts')
  for (const distance of [0, 0.08, 0.12, 0.13, 0.18])
    assert.equal(liquidHalfSpan(distance, 0.075), 0)
  let previous = 0
  for (let distance = 0.18; distance < 0.5; distance += 0.001) {
    const next = liquidHalfSpan(distance, 0.075)
    assert.ok(next >= previous && next - previous < 0.03)
    previous = next
  }
})

test('liquid drift stays small and continuous across animation frames', async () => {
  const { liquidDrift } = await import('../src/motion.ts')
  let previous = liquidDrift(0)
  for (let frame = 1; frame < 3600; frame++) {
    const drift = liquidDrift(frame / 60)
    assert.ok(Math.hypot(...drift) * 0.075 < 0.009)
    assert.ok(Math.hypot(...drift.map((x, i) => x - previous[i])) * 0.075 < 0.0002)
    previous = drift
  }
})

test('stretched liquid endpoints reach the palm anchors', async () => {
  const { liquidHalfSpan } = await import('../src/motion.ts')
  for (const distance of [0.3, 0.4, 0.5])
    assert.ok(Math.abs(liquidHalfSpan(distance, 0.075) * 0.15 - distance) < 1e-12)
})

test('compression starts after fusion and lifts the center without moving the ends', async () => {
  const { liquidCompression, liquidSqueezeLift, liquidHalfSpan } = await import('../src/motion.ts')
  let last = 0
  for (let distance = 0.3; distance >= 0; distance -= 0.001) {
    const squeeze = liquidCompression(distance)
    assert.ok(squeeze >= last && squeeze <= 1)
    if (squeeze > 0) assert.equal(liquidHalfSpan(distance, 0.075), 0)
    assert.ok(Math.abs(liquidSqueezeLift(0, squeeze)) < 1e-12)
    assert.ok(Math.abs(liquidSqueezeLift(4, squeeze)) < 1e-12)
    assert.ok(liquidSqueezeLift(2, squeeze) <= liquidSqueezeLift(1, squeeze))
    last = squeeze
  }
  assert.equal(liquidCompression(0.07), 1)
  assert.equal(liquidCompression(0.18), 0)
})
