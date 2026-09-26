// Exponential response measured in seconds, independent of display refresh rate.
export function followAmount(dt: number, rate = 22): number {
  return -Math.expm1(-Math.max(0, dt) * rate)
}

// Exact critically damped spring for a target held constant during this frame.
export function springStep(
  position: number,
  velocity: number,
  target: number,
  dt: number,
  frequency = 16,
): [number, number] {
  const elapsed = Math.max(0, dt)
  const offset = position - target
  const decay = Math.exp(-frequency * elapsed)
  const impulse = velocity + frequency * offset
  return [
    target + (offset + impulse * elapsed) * decay,
    (velocity - frequency * impulse * elapsed) * decay,
  ]
}

export function metalHalfSpan(distance: number, unit: number): number {
  return Math.max(0, distance) / (2 * unit)
}

// A forgiving merge zone: palms need not touch or occlude each other.
export function liquidHalfSpan(distance: number, unit: number): number {
  const opening = Math.min(1, Math.max(0, (distance - 0.18) / 0.12))
  const blend = opening * opening * (3 - 2 * opening)
  return metalHalfSpan(distance, unit) * blend
}

// Continuous, bounded drift in material units, independent of detector updates.
export function liquidDrift(time: number): [number, number, number] {
  return [
    0.065 * Math.sin(time * 0.71) + 0.025 * Math.sin(time * 1.13),
    0.055 * Math.sin(time * 0.53 + 1.2),
    0.035 * Math.sin(time * 0.83 + 2.4),
  ]
}

// Compression begins only after the two lobes have merged (distances in metres).
export function liquidCompression(distance: number): number {
  const amount = Math.min(1, Math.max(0, (0.16 - distance) / 0.09))
  return amount * amount * (3 - 2 * amount)
}

// Camera coordinates use negative Y for up. Keep the ends planted while the
// center rises like a soft cushion between the palms.
export function liquidSqueezeLift(node: number, compression: number): number {
  return -0.65 * Math.sin((Math.PI * node) / 4) ** 2 * compression
}
