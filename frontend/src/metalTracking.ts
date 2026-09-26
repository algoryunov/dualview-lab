import { metricHand, palmCenter } from './spatialMath.ts'
import type { Vec3 } from './spatialMath'
import type { Telemetry } from './types'

type Pose = { points: Vec3[]; velocity: Vec3; seen: number }
// Visual continuity only: predictions never feed calibration or gesture commands.
export class MetalHandTracker {
  private poses: (Pose | null)[] = [null, null]
  private received = -1
  predicted = false

  update(telemetry: Telemetry | null, received: number, now: number, usable: boolean): Vec3[][] {
    this.predicted = false
    if (!usable || !telemetry || now - received >= 650) {
      this.poses = [null, null]
      this.received = -1
      return []
    }
    const hand = telemetry.hand_tracking
    if (received !== this.received) {
      this.received = received
      const samples = [hand.filtered_points_m, hand.secondary_filtered_points_m]
      const confidence = [hand.confidence, hand.secondary_confidence ?? 0]
      const sources = [hand.source, hand.secondary_source]
      samples.forEach((points, i) => {
        if (
          !metricHand(points) ||
          confidence[i] < 0.55 ||
          sources[i] === 'held' ||
          sources[i] === 'predicted' ||
          sources[i] === 'missing'
        )
          return
        const old = this.poses[i]
        const dt = old ? (received - old.seen) / 1000 : 0
        let velocity: Vec3 = [0, 0, 0]
        if (old && dt > 0.015 && dt < 0.35) {
          const previous = palmCenter(old.points),
            current = palmCenter(points)
          const delta = current.map((x, axis) => x - previous[axis]) as Vec3
          // Reacquired identities or large geometry jumps must not launch the material.
          if (Math.hypot(...delta) < 0.08) {
            velocity = delta.map((x, axis) => old.velocity[axis] * 0.65 + (x / dt) * 0.35) as Vec3
            const scale = Math.min(1, 0.5 / Math.max(1e-9, Math.hypot(...velocity)))
            velocity = velocity.map((x) => x * scale) as Vec3
          }
        }
        this.poses[i] = { points: points.map((p) => [...p] as Vec3), velocity, seen: received }
      })
    }
    return this.poses.flatMap((pose, i) => {
      if (!pose) return []
      const age = Math.max(0, (now - pose.seen) / 1000)
      if (age >= 0.35) {
        this.poses[i] = null
        return []
      }
      // Damped constant-velocity prediction saturates at 2.5 cm, then expires.
      const horizon = 0.05 * (1 - Math.exp(-age / 0.05))
      const source = i === 0 ? hand.source : hand.secondary_source
      if (
        source === 'held' ||
        source === 'predicted' ||
        source === 'missing' ||
        hand.status !== 'tracked' ||
        !metricHand(i === 0 ? hand.filtered_points_m : hand.secondary_filtered_points_m)
      )
        this.predicted = true
      return [pose.points.map((p) => p.map((x, axis) => x + pose.velocity[axis] * horizon) as Vec3)]
    })
  }
}
