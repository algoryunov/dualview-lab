export type Vec3 = [number, number, number]
export type EffectCamera = {
  width: number
  height: number
  intrinsics: number[]
  distortion: number[]
  rotation: number[][]
  translation: number[]
}
export function metricHand(points: number[][] | null | undefined): points is Vec3[] {
  return (
    points?.length === 21 &&
    points.every(
      (p) =>
        p.length === 3 &&
        p.every(Number.isFinite) &&
        p[2] > 0.03 &&
        Math.abs(p[0]) < 3 &&
        Math.abs(p[1]) < 3 &&
        p[2] < 5,
    )
  )
}
export function palmCenter(hand: Vec3[]): Vec3 {
  return [0, 1, 2].map(
    (axis) => [0, 5, 9, 13, 17].reduce((sum, i) => sum + hand[i][axis], 0) / 5,
  ) as Vec3
}
export function toCamera(p: Vec3, c: EffectCamera): Vec3 {
  return c.rotation.map((row, i) => row.reduce((s, v, j) => s + v * p[j], c.translation[i])) as Vec3
}
export function projectionMatches(
  c: EffectCamera | undefined,
  width: number | null,
  height: number | null,
) {
  return !!c && c.width === width && c.height === height
}
export function smoothAngle(current: number, target: number, amount: number) {
  return current + Math.atan2(Math.sin(target - current), Math.cos(target - current)) * amount
}
