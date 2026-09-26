import { useCallback, useEffect, useRef, useState } from 'react'
import { initialTelemetry } from '../initialTelemetry'
import { RealtimeConnection } from './rtc'
import type { CameraId } from './rtc'
export function useRealtime() {
  const [telemetry, setTelemetry] = useState(initialTelemetry)
  const [streams, setStreams] = useState<Partial<Record<CameraId, MediaStream>>>({})
  const [status, setStatus] = useState('connecting')
  const [error, setError] = useState('')
  const connection = useRef<RealtimeConnection | null>(null)
  useEffect(() => {
    let stopped = false
    let retry: number | undefined
    let backoff = 1000
    const connect = () => {
      if (stopped) return
      setStatus('connecting')
      connection.current = new RealtimeConnection({
        role: 'dashboard',
        onTelemetry: (value) => {
          setTelemetry(value)
          setError('')
          backoff = 1000
        },
        onTrack: (camera, stream) => setStreams((current) => ({ ...current, [camera]: stream })),
        onError: setError,
        onState: (value) => {
          if (stopped) return
          setStatus(value)
          if (value === 'disconnected') {
            setStreams({})
            setTelemetry(initialTelemetry)
            clearTimeout(retry)
            retry = window.setTimeout(connect, backoff)
            backoff = Math.min(backoff * 2, 10000)
          }
        },
      })
    }
    connect()
    return () => {
      stopped = true
      clearTimeout(retry)
      connection.current?.close()
    }
  }, [])
  const command = useCallback(async (name: string, payload?: Record<string, unknown>) => {
    if (!connection.current) throw new Error('Realtime connection is not ready.')
    await connection.current.command(name, payload)
  }, [])
  return { telemetry, streams, status, error, command }
}
