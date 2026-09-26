import { useEffect, useRef, useState } from 'react'
import { HandOverlay } from '../../HandOverlay'
import type { CameraId } from '../../transport/rtc'
import type { Telemetry } from '../../types'
export function Camera({
  id,
  stream,
  telemetry,
}: {
  id: CameraId
  stream?: MediaStream
  telemetry: Telemetry
}) {
  const [decoded, setDecoded] = useState('')
  const video = useRef<HTMLVideoElement>(null)
  useEffect(() => {
    if (video.current) video.current.srcObject = stream ?? null
  }, [stream])
  return (
    <section className="camera-card">
      <div className="card-label">
        {id === 'laptop' ? 'Laptop camera' : 'Phone camera'}
        <span>{telemetry[id].available ? 'Live' : 'Waiting'}</span>
      </div>
      <div className="camera-feed">
        <video
          ref={video}
          autoPlay
          muted
          playsInline
          onResize={() => {
            const current = video.current
            setDecoded(current?.videoWidth ? `${current.videoWidth} × ${current.videoHeight}` : '')
          }}
        />
        <HandOverlay
          cameraId={id}
          hands={telemetry.hand_tracking[`${id}_hands_landmarks_normalized`]}
          cube={null}
          health={telemetry[id]}
          tracked={telemetry.hand_tracking.status === 'tracked'}
        />
      </div>
      <small>
        Received by server: {telemetry[id].width ?? '—'} × {telemetry[id].height ?? '—'} · Preview:{' '}
        {decoded || 'Waiting'}
      </small>
    </section>
  )
}
