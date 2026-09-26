import test from 'node:test'
import assert from 'node:assert/strict'
import * as T from 'three'
import { alignStageCamera } from '../src/stageCamera.ts'

test('landmark projection matches metal shader in wide and portrait stages, including depth', () => {
  for (const [width, height] of [
    [1440, 600],
    [390, 700],
  ]) {
    const eye = new T.Vector3(-1.2, -0.4, -4.7)
    const target = new T.Vector3(0, -0.05, 0)
    const camera = new T.PerspectiveCamera((2 * Math.atan(0.46) * 180) / Math.PI, 1, 0.01, 20)
    alignStageCamera(camera, eye, target, width, height)
    const forward = target.clone().sub(eye).normalize()
    const right = new T.Vector3().crossVectors(new T.Vector3(0, 1, 0), forward).normalize()
    const down = new T.Vector3().crossVectors(forward, right)
    for (const point of [new T.Vector3(0.3, 0.4, 0), new T.Vector3(-0.2, -0.1, 1.5)]) {
      const ray = point.clone().sub(eye)
      const distance = ray.dot(forward)
      const expectedX =
        (width / height > 1.5 ? 0.56 : 0.5) * width + (ray.dot(right) / distance / 0.92) * height
      const expectedY = 0.5 * height + (ray.dot(down) / distance / 0.92) * height
      const projected = point.clone().project(camera)
      assert.ok(Math.abs(((projected.x + 1) * width) / 2 - expectedX) < 1e-8)
      assert.ok(Math.abs(((1 - projected.y) * height) / 2 - expectedY) < 1e-8)
    }
  }
})
