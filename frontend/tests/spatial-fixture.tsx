// Manual WebGL fixture, served by Vite only. No cameras, files, or network requests.
import { useEffect, useState } from 'react'
import { createRoot } from 'react-dom/client'
import { LiquidMetal } from '../src/LiquidMetal'
import { SpatialContext } from '../src/spatialContext'
import { changeMetalSettings } from '../src/metalSettings'
import type { Telemetry } from '../src/types'
import type { EffectCamera } from '../src/spatialMath'
declare global {
  interface Window {
    metalSamples: Record<string, unknown>[]
  }
}
window.metalSamples = []

const identity = [
  [1, 0, 0],
  [0, 1, 0],
  [0, 0, 1],
]
const laptop: EffectCamera = {
  width: 640,
  height: 480,
  intrinsics: [550, 550, 320, 240],
  distortion: [0, 0, 0, 0, 0, 0, 0, 0],
  rotation: identity,
  translation: [0, 0, 0],
}
const angle = 0.2
const phone: EffectCamera = {
  ...laptop,
  rotation: [
    [Math.cos(angle), 0, Math.sin(angle)],
    [0, 1, 0],
    [-Math.sin(angle), 0, Math.cos(angle)],
  ],
  translation: [-0.13, 0, 0.03],
}
function hand(x: number, z: number) {
  const p = Array.from({ length: 21 }, () => [x, 0, z])
  p[0] = [x, 0.055, z]
  for (let f = 0; f < 5; f++)
    for (let j = 0; j < 4; j++) p[1 + f * 4 + j] = [x + (f - 2) * 0.017, -j * 0.025, z - j * 0.006]
  return p
}
export function Fixture() {
  const [stale, setStale] = useState(false),
    [near, setNear] = useState(false),
    [depth, setDepth] = useState(false),
    [mask, setMask] = useState(false),
    [close, setClose] = useState(false),
    [single, setSingle] = useState(false),
    [tick, setTick] = useState(0)
  useEffect(() => {
    const id = setInterval(() => setTick((t) => t + 1), 60)
    return () => clearInterval(id)
  }, [])
  useEffect(() => changeMetalSettings({ occlusion: mask }), [mask])
  const health = { available: true, width: 640, height: 480, age_ms: stale ? 2000 : 20 }
  const telemetry = {
    laptop: health,
    phone: health,
    calibration: { status: 'calibrated' },
    hand_tracking: {
      status: 'tracked',
      confidence: 0.95,
      secondary_confidence: 0.95,
      filtered_points_m: hand(close ? 0 : depth ? 0 : -0.15, near ? 0.4 : 0.72),
      secondary_filtered_points_m: single
        ? null
        : close
          ? hand(tick % 2 ? 0.002 : -0.002, near ? 0.4 : 0.72)
          : hand(depth ? 0 : 0.15, (near ? 0.4 : 0.72) + (depth ? 0.3 : 0.06)),
    },
  } as Telemetry
  return (
    <SpatialContext.Provider
      value={{
        telemetry,
        received: performance.now(),
        cameras: { laptop, phone },
        reportMetal: (sample) => {
          window.metalSamples.push(sample)
          if (window.metalSamples.length > 200) window.metalSamples.shift()
        },
      }}
    >
      <header style={{ padding: 20 }}>
        <h1>Synthetic stereo QA</h1>
        <p>Left: laptop. Right: phone rotated 0.2 radians and translated −0.13 m. Tick {tick}.</p>
        <button onClick={() => setDepth(!depth)}>
          {depth ? 'Pull sideways' : 'Pull in depth'}
        </button>{' '}
        <button onClick={() => setNear(!near)}>{near ? 'Move farther' : 'Move closer'}</button>{' '}
        <button onClick={() => setStale(!stale)}>
          {stale ? 'Restore tracking' : 'Lose tracking'}
        </button>{' '}
        <button onClick={() => setMask(!mask)}>
          {mask ? 'Disable occlusion' : 'Enable occlusion'}
        </button>
        <button onClick={() => setClose(!close)}>
          {close ? 'Separate palms' : 'Bring palms together'}
        </button>
        <button onClick={() => setSingle(!single)}>
          {single ? 'Show second hand' : 'Hide second hand'}
        </button>
      </header>
      <div style={{ height: 600, position: 'relative' }}>
        <LiquidMetal preview />
      </div>
      <div style={{ display: 'flex', gap: 20, padding: 20 }}>
        {(['laptop', 'phone'] as const).map((cameraId) => (
          <div
            key={cameraId}
            style={{
              position: 'relative',
              width: 640,
              height: 480,
              background: 'repeating-linear-gradient(90deg,#24344b 0px,#24344b 79px,#40516a 80px)',
              overflow: 'hidden',
            }}
          >
            <LiquidMetal cameraId={cameraId} />
            <span style={{ position: 'absolute', top: 10, left: 10 }}>{cameraId}</span>
          </div>
        ))}
      </div>
    </SpatialContext.Provider>
  )
}
createRoot(document.getElementById('root')!).render(<Fixture />)
