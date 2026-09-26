import type { StreamHealth } from './types'
// A recent timestamp plus image dimensions is frame evidence, including with
// older servers whose phone availability incorrectly checks JPEG storage.
export function hasFreshFrame(stream: StreamHealth, maxAge = 650) {
  return (
    (stream.width ?? 0) > 0 &&
    (stream.height ?? 0) > 0 &&
    stream.age_ms !== null &&
    Number.isFinite(stream.age_ms) &&
    stream.age_ms >= 0 &&
    stream.age_ms < maxAge
  )
}
export function streamDelayMessage(name: string, stream: StreamHealth) {
  if (stream.age_ms === null || !(stream.width && stream.height))
    return `${name}: no frames received yet`
  return `${name}: last frame ${(Math.max(0, stream.age_ms) / 1000).toFixed(1)} s ago`
}
