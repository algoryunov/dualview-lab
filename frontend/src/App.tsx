import { useEffect, useRef, useState } from 'react'
import { SpatialTracking } from './SpatialTracking'
import { SceneView } from './SceneView'
import { effectModes, setDemoMode, useDemoMode } from './demoMode'
import { demoModels, ModelGestureLatch, nextModel, useModelSelection } from './modelSelection'
import { useRealtime } from './transport/useRealtime'
import { PairingDialog } from './features/pairing/PairingDialog'
import { SetupPanel } from './features/setup/SetupPanel'
import './app.css'
export function App() {
  const { telemetry: t, streams, status, error: transportError, command } = useRealtime()
  const [tab, setTab] = useState<'metal' | 'experiments' | 'setup'>('metal')
  const [experiment, setExperiment] = useState<'models' | 'trails' | 'energy'>('models')
  const [pairing, setPairing] = useState(false)
  const [error, setError] = useState('')
  const [pending, setPending] = useState(false)
  const mode = useDemoMode()
  const model = useModelSelection()
  const latch = useRef(new ModelGestureLatch())
  useEffect(() => {
    setDemoMode(tab === 'experiments' ? experiment : 'metal')
  }, [tab, experiment])
  useEffect(() => {
    let cancelled = false
    if (status === 'connected')
      void command('interaction.model', { model_id: demoModels[model].id }).catch((error) => {
        if (!cancelled) setError(String(error))
      })
    return () => {
      cancelled = true
    }
  }, [model, status, command])
  useEffect(() => {
    const i = t.interaction
    if (
      mode === 'models' &&
      latch.current.update(
        i.gesture,
        i.hold_elapsed_ms,
        t.hand_tracking.status === 'tracked' && !i.pinch && !i.interaction_block_reason,
      )
    )
      nextModel()
  }, [t, mode])
  async function run(action: () => Promise<void>) {
    setPending(true)
    setError('')
    try {
      await action()
    } catch (error) {
      setError(error instanceof Error ? error.message : String(error))
    } finally {
      setPending(false)
    }
  }
  return (
    <SpatialTracking telemetry={t} command={command}>
      <main className="application">
        <header className="app-header">
          <a className="brand" href="/">
            DUALVIEW<span>Liquid Metal Lab</span>
          </a>
          <nav aria-label="Main navigation">
            {(['metal', 'experiments'] as const).map((value) => (
              <button
                key={value}
                aria-current={tab === value ? 'page' : undefined}
                onClick={() => setTab(value)}
              >
                {value === 'metal' ? 'Liquid Metal' : 'Other Modes'}
              </button>
            ))}
          </nav>
          <button
            className="settings-button"
            aria-current={tab === 'setup' ? 'page' : undefined}
            onClick={() => setTab('setup')}
          >
            <span aria-hidden="true">⚙</span> Settings
          </button>
        </header>
        {(error || transportError) && (
          <p role="alert" className="app-error">
            {error || transportError}
          </p>
        )}
        {tab !== 'setup' ? (
          <>
            <div className="stage-toolbar">
              {tab === 'experiments' ? (
                <div role="group" aria-label="Experiment">
                  {(['models', 'trails', 'energy'] as const).map((id) => (
                    <button
                      key={id}
                      aria-pressed={experiment === id}
                      onClick={() => setExperiment(id)}
                    >
                      {effectModes.find((m) => m.id === id)?.label}
                    </button>
                  ))}
                </div>
              ) : (
                <span>01 / INTERACTIVE MATERIAL</span>
              )}
              {tab === 'experiments' && experiment === 'models' && (
                <button
                  disabled={pending || status !== 'connected'}
                  onClick={() => void run(() => command('interaction.reset'))}
                >
                  Reset position
                </button>
              )}
              <span>{status === 'connected' ? '● Local connection' : '○ Connecting'}</span>
            </div>
            <section
              className="main-stage"
              aria-label={tab === 'metal' ? 'Liquid Metal stage' : 'Experiment stage'}
            >
              {tab === 'experiments' && experiment === 'models' && (
                <p role="status">
                  Pinch with one hand to move; pinch with both hands and change their distance to
                  scale.
                </p>
              )}
              <SceneView
                effectHands={t.hand_tracking.laptop_hands_landmarks_normalized}
                effectHealth={t.laptop}
                points={t.hand_tracking.filtered_points_m}
                secondaryPoints={t.hand_tracking.secondary_filtered_points_m}
                confidence={t.hand_tracking.landmark_confidence}
                cursor={t.interaction.cursor_m}
                objectPosition={t.interaction.object_position_m}
                objectScale={t.interaction.object_scale_m}
                objectRotation={t.interaction.object_rotation_quat}
                state={t.interaction.state}
                objectStatus={t.interaction.object_status}
              />
            </section>
          </>
        ) : (
          <SetupPanel
            onConnectPhone={() => setPairing(true)}
            telemetry={t}
            streams={streams}
            status={status}
            pending={pending}
            run={run}
            command={command}
          />
        )}
        <footer className="app-footer">
          <span>Local vision. Real-time interaction.</span>
          <button onClick={() => setTab('setup')}>
            Tracking:{' '}
            {t.hand_tracking.source === 'predicted'
              ? 'briefly predicting hand motion'
              : t.hand_tracking.source === 'held'
                ? 'last position held'
                : t.hand_tracking.source === 'measured'
                  ? 'measured'
                  : 'unavailable'}
          </button>
        </footer>
        {pairing && <PairingDialog onClose={() => setPairing(false)} />}
      </main>
    </SpatialTracking>
  )
}
