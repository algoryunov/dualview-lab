import { useCallback, useMemo, useRef, useState } from 'react'
import type { ReactNode } from 'react'
import type { Telemetry } from './types'
import { SpatialContext } from './spatialContext'
export function SpatialTracking({
  telemetry,
  children,
  command,
}: {
  telemetry: Telemetry
  children: ReactNode
  command?: (name: string, payload?: Record<string, unknown>) => Promise<void>
}) {
  const pending = useRef(false)
  const [metalLogStatus, setMetalLogStatus] = useState('Waiting for samples')
  const stamped = useMemo(() => ({ telemetry, received: performance.now() }), [telemetry])
  const reportMetal = useCallback(
    (sample: Record<string, unknown>) => {
      if (!command || pending.current) return
      pending.current = true
      void command('metal.sample', { sample })
        .then(() => setMetalLogStatus('Recording'))
        .catch((error: unknown) => {
          setMetalLogStatus(error instanceof Error ? error.message : 'Recording unavailable')
        })
        .finally(() => {
          pending.current = false
        })
    },
    [command],
  )
  const value = useMemo(
    () => ({
      ...stamped,
      cameras: telemetry.cameras ?? null,
      cameraIssue: telemetry.cameras ? '' : 'Calibrate both cameras to align the 3D overlay.',
      reportMetal,
      metalLogStatus,
    }),
    [telemetry, reportMetal, stamped, metalLogStatus],
  )
  return <SpatialContext.Provider value={value}>{children}</SpatialContext.Provider>
}
