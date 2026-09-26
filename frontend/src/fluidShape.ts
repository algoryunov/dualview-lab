// A swept, tapered body with an approximate conserved volume. This is a
// lightweight visual model, not a fluid solver. The budget includes end caps.
export function sweptVolume(lengths: number[], radii: number[]): number {
  let volume = ((2 * Math.PI) / 3) * (radii[0] ** 3 + radii[4] ** 3)
  for (let i = 0; i < 4; i++)
    volume +=
      (Math.PI * lengths[i] * (radii[i] ** 2 + radii[i] * radii[i + 1] + radii[i + 1] ** 2)) / 3
  return volume
}

export function fluidRadii(
  lengths: number[],
  time: number,
  energy: number,
  compression = 0,
): number[] {
  const stretch =
    Math.min(1, lengths.reduce((sum, length) => sum + length, 0) / 1.5) *
    (1 - Math.min(1, Math.max(0, compression)))
  const compact = [0.76, 1.08, 1.2, 1.08, 0.76]
  // Keep the volume near each palm, with a slender, continuous liquid bridge.
  const weights = [1.5, 0.48, 0.34, 0.48, 1.5]
    .map((w, i) => compact[i] + (w - compact[i]) * stretch)
    .map(
      (weight, i) =>
        weight *
        (1 + (0.1 + 0.16 * Math.min(1, Math.max(0, energy))) * Math.sin(time * 1.8 - i * 1.25)),
    )
  let low = 0,
    high = 2
  for (let i = 0; i < 20; i++) {
    const scale = (low + high) / 2
    if (
      sweptVolume(
        lengths,
        weights.map((w) => w * scale),
      ) > 3.6
    )
      high = scale
    else low = scale
  }
  return weights.map((weight) => (weight * (low + high)) / 2)
}

// Compute the curve once per animation frame, not once per ray-march step.
export function fluidNodes(points: number[][]): number[][] {
  return Array.from({ length: 9 }, (_, n) => {
    const x = n / 2,
      i = Math.min(3, Math.floor(x)),
      u = x - i
    const [a, b, c, d] = [
      points[Math.max(0, i - 1)],
      points[i],
      points[i + 1],
      points[Math.min(4, i + 2)],
    ]
    return b.map(
      (value, j) =>
        0.5 *
        (2 * value +
          (-a[j] + c[j]) * u +
          (2 * a[j] - 5 * value + 4 * c[j] - d[j]) * u * u +
          (-a[j] + 3 * value - 3 * c[j] + d[j]) * u * u * u),
    )
  })
}
