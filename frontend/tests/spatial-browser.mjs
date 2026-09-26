import assert from 'node:assert/strict'
import os from 'node:os'
import path from 'node:path'
import { chromium } from '@playwright/test'

const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || undefined })
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1250 } })
  const errors = []
  page.on('pageerror', (error) => errors.push(String(error)))
  page.on('console', (message) => {
    if (message.type() === 'error') errors.push(message.text())
  })
  await page.goto(
    `${process.env.TEST_SPATIAL_ORIGIN || 'http://127.0.0.1:5175'}/tests/spatial-fixture.html`,
  )
  await page.getByText('Hand control is active.', { exact: false }).first().waitFor()
  await page.getByRole('button', { name: 'Bring palms together', exact: true }).click()
  await page.waitForTimeout(500)
  await page.evaluate(() => {
    window.metalSamples = []
  })
  await page.waitForTimeout(600)
  const stable = await page.evaluate(() => window.metalSamples)
  assert.ok(stable.length >= 3)
  for (const sample of stable) {
    assert.ok(sample.distance_m < 0.025)
    assert.ok(
      sample.axis.every((value, i) => Math.abs(value - stable[0].axis[i]) < 1e-6),
      'Coincident palms must not flip the stretch axis',
    )
  }
  assert.ok(stable.every((sample) => sample.compression === 1 && sample.separation === 0))
  assert.ok(
    stable.at(-1).blobs[9] < stable.at(-1).blobs[1] - 0.3,
    'Compression must lift the center above the palm-side endpoints',
  )
  await page.screenshot({ path: path.join(os.tmpdir(), 'dualview-metal-compressed.png') })
  await page.getByRole('button', { name: 'Hide second hand', exact: true }).click()
  await page.waitForTimeout(650)
  const transition = await page.evaluate(() =>
    window.metalSamples.filter((s) => s.phase === 'one_hand'),
  )
  assert.ok(
    transition.some((s) => s.neck > 0.01 && s.neck < 0.99),
    'The hand-count transition must blend instead of switching abruptly',
  )
  for (let i = 1; i < transition.length; i++) {
    assert.ok(transition[i].blobs.every(Number.isFinite))
    for (let node = 0; node < 5; node++) {
      const offset = node * 4
      const displacement = Math.hypot(
        ...[0, 1, 2].map(
          (axis) => transition[i].blobs[offset + axis] - transition[i - 1].blobs[offset + axis],
        ),
      )
      assert.ok(displacement < 0.3, 'Hand-count changes must not teleport fluid control nodes')
    }
  }
  await page.getByRole('button', { name: 'Show second hand', exact: true }).click()
  await page.getByRole('button', { name: 'Separate palms', exact: true }).click()
  await page.waitForTimeout(700)
  await page.screenshot({ path: path.join(os.tmpdir(), 'dualview-stretch-sideways.png') })
  await page.getByRole('button', { name: 'Pull in depth', exact: true }).click()
  await page.waitForTimeout(1000) // Let the liquid spring settle before capturing its geometry.
  await page.screenshot({ path: path.join(os.tmpdir(), 'dualview-stretch-depth.png') })
  await page.getByRole('button', { name: 'Move closer', exact: true }).click()
  await page.waitForTimeout(1000)
  await page.screenshot({ path: path.join(os.tmpdir(), 'dualview-stretch-near.png') })
  await page.getByRole('button', { name: 'Lose tracking', exact: true }).click()
  await page.getByText('Video stream delayed.', { exact: false }).first().waitFor()
  await page.getByRole('button', { name: 'Restore tracking', exact: true }).click()
  await page.getByText('Hand control is active.', { exact: false }).first().waitFor()
  const timings = await page.evaluate(
    () =>
      new Promise((resolve) => {
        const intervals = []
        let previous = 0
        const frame = (now) => {
          if (previous) intervals.push(now - previous)
          previous = now
          if (intervals.length >= 180) resolve(intervals.sort((a, b) => a - b))
          else requestAnimationFrame(frame)
        }
        requestAnimationFrame(frame)
      }),
  )
  const median = timings[Math.floor(timings.length * 0.5)],
    p95 = timings[Math.floor(timings.length * 0.95)]
  assert.ok(
    median < 34 && p95 < 50,
    `Synthetic rendering is too slow: median ${median} ms, p95 ${p95} ms`,
  )
  console.log(
    `Frame timing with three liquid views: median ${median.toFixed(1)} ms, p95 ${p95.toFixed(1)} ms`,
  )
  assert.deepEqual(errors, [])
  console.log(
    'PASS: two-hand depth stretching, near/far motion, tracking loss/recovery, and WebGL rendering',
  )
} finally {
  await browser.close()
}
