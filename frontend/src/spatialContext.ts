import { createContext, useContext } from 'react'
import type { Telemetry } from './types'
import type { EffectCamera } from './spatialMath'
export type SpatialState = {
  telemetry: Telemetry | null
  received: number
  cameraIssue?: string
  cameras: { laptop: EffectCamera; phone: EffectCamera } | null
  reportMetal?: (sample: Record<string, unknown>) => void
  metalLogStatus?: string
}
export const SpatialContext = createContext<SpatialState>({
  telemetry: null,
  received: 0,
  cameras: null,
})
export function useSpatialTracking() {
  return useContext(SpatialContext)
}
