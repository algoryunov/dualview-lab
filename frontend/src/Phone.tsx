import { useEffect, useRef, useState } from 'react'
import { RealtimeConnection } from './transport/rtc'
function savedCapturePreference(key: 'camera' | 'resolution'): string | undefined {
  try {
    return JSON.parse(localStorage.getItem('dualview.phone.capture') || '{}')[key]
  } catch {
    return undefined
  }
}
export function Phone() {
  const video = useRef<HTMLVideoElement>(null)
  const connection = useRef<RealtimeConnection | null>(null)
  const stream = useRef<MediaStream | null>(null)
  const generation = useRef(0)
  const [capture, setCapture] = useState('')
  const [quality, setQuality] = useState('')
  const [resolution, setResolution] = useState<'720' | '1080'>(() =>
    savedCapturePreference('resolution') === '1080' ? '1080' : '720',
  )
  const [camera, setCamera] = useState<'user' | 'environment'>(() =>
    savedCapturePreference('camera') === 'environment' ? 'environment' : 'user',
  )
  const [state, setState] = useState('ready')
  const [message, setMessage] = useState('Choose a camera, then start it.')
  const active = ['requesting', 'connecting', 'connected'].includes(state)
  useEffect(() => {
    try {
      localStorage.setItem('dualview.phone.capture', JSON.stringify({ camera, resolution }))
    } catch {
      /* Capture still works when browser storage is disabled. */
    }
  }, [camera, resolution])
  function release() {
    generation.current++
    setCapture('')
    setQuality('')
    connection.current?.phoneEvent('user_stop')
    connection.current?.close()
    connection.current = null
    stream.current?.getTracks().forEach((track) => track.stop())
    stream.current = null
    if (video.current) video.current.srcObject = null
  }
  useEffect(
    () => () => {
      generation.current++
      connection.current?.close()
      stream.current?.getTracks().forEach((track) => track.stop())
    },
    [],
  )
  useEffect(() => {
    if (state !== 'connected') return
    let cancelled = false,
      pending = false
    let previous: { bytes: number; timestamp: number } | null = null
    const timer = window.setInterval(async () => {
      if (pending) return
      pending = true
      try {
        const report = await connection.current?.videoStats()
        if (cancelled || !report) return
        const mbps =
          previous && report.timestamp > previous.timestamp
            ? Math.max(
                0,
                ((report.bytes - previous.bytes) * 8) /
                  (report.timestamp - previous.timestamp) /
                  1000,
              ).toFixed(1)
            : '—'
        previous = report
        setQuality(
          `Sending: ${report.width} × ${report.height} · ${Math.round(report.fps)} fps · ${mbps} Mbit/s · Limit: ${report.limitation}`,
        )
      } catch {
        /* Diagnostics must not interrupt an active camera. */
      } finally {
        pending = false
      }
    }, 1000)
    return () => {
      cancelled = true
      clearInterval(timer)
    }
  }, [state])
  async function start() {
    release()
    const attempt = generation.current
    setState('requesting')
    setMessage('Requesting camera access…')
    try {
      let local: MediaStream
      try {
        local = await navigator.mediaDevices.getUserMedia({
          video: {
            facingMode: { ideal: camera },
            width: { min: 1280, ideal: resolution === '1080' ? 1920 : 1280 },
            height: { min: 720, ideal: resolution === '1080' ? 1080 : 720 },
            frameRate: { ideal: 15, max: 30 },
          },
          audio: false,
        })
      } catch (error) {
        if (!(error instanceof DOMException) || error.name !== 'OverconstrainedError') throw error
        if (attempt !== generation.current) return
        local = await navigator.mediaDevices.getUserMedia({
          video: {
            facingMode: { ideal: camera },
            width: { ideal: 1280 },
            height: { ideal: 720 },
            frameRate: { ideal: 15, max: 30 },
          },
          audio: false,
        })
      }
      if (attempt !== generation.current) {
        local.getTracks().forEach((track) => track.stop())
        return
      }
      const track = local.getVideoTracks()[0]
      track.contentHint = 'detail'
      const settings = track.getSettings()
      setCapture(
        `Capture: ${settings.width ?? '—'} × ${settings.height ?? '—'} · ${Math.round(settings.frameRate ?? 0)} fps`,
      )
      stream.current = local
      if (video.current) {
        video.current.srcObject = local
        await video.current.play()
      }
      if (attempt !== generation.current) return
      setState('connecting')
      setMessage('Connecting to the local server…')
      connection.current = new RealtimeConnection({
        role: 'phone',
        stream: local,
        session: new URLSearchParams(location.search).get('session'),
        onError: (message) => {
          setMessage(message)
          release()
          setState('error')
        },
        onState: (value) => {
          if (attempt !== generation.current) return
          setState(value)
          if (value === 'connected')
            setMessage('Camera connected. Video is processed on the local server.')
          if (value === 'disconnected') {
            release()
            setMessage('Connection lost. Start the camera to reconnect.')
          }
        },
      })
    } catch (error) {
      if (attempt === generation.current) {
        release()
        setState('error')
        setMessage(error instanceof Error ? error.message : 'Camera setup failed.')
      }
    }
  }
  return (
    <main className="phone">
      <p className="eyebrow">DUALVIEW / CAMERA NODE</p>
      <h1>Phone camera</h1>
      <video ref={video} autoPlay playsInline muted />
      <p className={`phone-status ${state}`} role="status">
        {message}
      </p>
      {capture && <p className="video-quality">{capture}</p>}
      {quality && <p className="video-quality">{quality}</p>}
      <div className="phone-field">
        <label htmlFor="phone-camera">Camera</label>
        <select
          id="phone-camera"
          disabled={active}
          value={camera}
          onChange={(e) => setCamera(e.target.value as 'user' | 'environment')}
        >
          <option value="user">Front camera</option>
          <option value="environment">Rear camera</option>
        </select>
      </div>
      <div className="phone-field">
        <label htmlFor="capture-quality">Capture quality</label>
        <select
          id="capture-quality"
          disabled={active}
          value={resolution}
          onChange={(event) => setResolution(event.target.value as '720' | '1080')}
        >
          <option value="720">HD · 720p</option>
          <option value="1080">Full HD · 1080p</option>
        </select>
      </div>
      <p className="privacy">
        Full HD needs more encoding power and bandwidth. Changing the camera or its crop may require
        recalibration.
      </p>
      {active ? (
        <button
          onClick={() => {
            release()
            setState('stopped')
            setMessage('Camera stopped.')
          }}
        >
          Stop camera
        </button>
      ) : (
        <button onClick={() => void start()}>Start camera</button>
      )}
      <p className="privacy">
        Video travels over WebRTC to your local server. No recording is enabled.
      </p>
    </main>
  )
}
