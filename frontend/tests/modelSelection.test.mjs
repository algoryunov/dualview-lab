import test from 'node:test'
import assert from 'node:assert/strict'
import { ModelGestureLatch } from '../src/modelSelection.ts'

test('pointing hold switches once and requires release', () => {
  const latch = new ModelGestureLatch()
  assert.equal(latch.update('Pointing_Up', 500, true), false)
  assert.equal(latch.update('Pointing_Up', 1000, true), true)
  assert.equal(latch.update('Pointing_Up', 2000, true), false)
  latch.update(null, 0, true)
  assert.equal(latch.update('Pointing_Up', 1000, true), true)
})
test('tracking loss or grabbing cancels hold until release', () => {
  const latch = new ModelGestureLatch()
  assert.equal(latch.update('Pointing_Up', 500, false), false)
  assert.equal(latch.update('Pointing_Up', 1500, true), false)
  latch.update('Thumb_Up', 1000, true)
  assert.equal(latch.update('Pointing_Up', 1000, true), true)
})
