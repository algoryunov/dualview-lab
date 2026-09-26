import { hasFreshFrame, streamDelayMessage } from './streamHealth.ts'
import type { Telemetry } from './types'
export function metalBlockReason(t: Telemetry | null, age: number): string {
  if (!t || age >= 650) return 'No fresh server data. Check the connection.'
  const delayed = [
    ['Laptop', t.laptop],
    ['Phone', t.phone],
  ] as const
  const missing = delayed.filter(([, stream]) => !hasFreshFrame(stream))
  if (missing.length)
    return `Video stream delayed. ${missing.map(([name, stream]) => streamDelayMessage(name, stream)).join('; ')}. Check the camera connection.`
  if (t.calibration.status !== 'calibrated') return 'Calibrate the camera pair first.'
  const h = t.hand_tracking
  if (h.reason === 'frame_pair_out_of_sync')
    return 'Camera frames are out of sync. Check the phone connection and inference diagnostics.'
  if (h.reason === 'stereo_geometry_invalid')
    return 'The camera pair cannot reconstruct a valid 3D hand. Check calibration and palm visibility in both views.'
  if (h.reason === 'reprojection_residual_too_high')
    return 'The cameras disagree on the hand position. Show one palm to both cameras. Recalibrate if the error persists.'
  if (h.reason === 'hand_lost_in_one_or_both_views')
    return 'Hand lost in one camera. Open Setup and make your entire palm visible to both devices.'
  if (h.status !== 'tracked')
    return 'Waiting for stable 3D tracking. Show one open palm to both cameras.'
  if (h.confidence < 0.55)
    return `3D tracking confidence: ${Math.round(h.confidence * 100)}%; 55% required. Hold your palm steady in both views.`
  return 'No usable 3D hand points. Check palm visibility.'
}
