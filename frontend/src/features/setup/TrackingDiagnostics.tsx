import type { Telemetry } from '../../types'

type Props = {
  telemetry: Telemetry
  pending: boolean
  start: (phase: string) => void
  timing: (action: 'apply' | 'reset') => void
}
const ms = (value: number | null | undefined) => (value == null ? '—' : `${value.toFixed(1)} ms`)
export function TrackingDiagnostics({ telemetry: t, pending, start, timing }: Props) {
  const p = t.processing
  const transport = p.phone_transport
  const phase = p.diagnostic_phase ?? 'idle'
  const estimate = p.timing_estimate
  const source = t.hand_tracking.source
  return (
    <section aria-label="Tracking quality">
      <h2>Tracking quality</h2>
      <p>
        {source === 'measured'
          ? 'Measured position'
          : source === 'predicted'
            ? 'Briefly predicting hand motion'
            : source === 'held'
              ? 'Last position held temporarily'
              : 'No measured position'}
      </p>
      <dl className="timing-metrics">
        <div>
          <dt>Pair difference · receive time</dt>
          <dd>{ms(t.hand_tracking.timing_error_ms)}</dd>
        </div>
        <div>
          <dt>Geometry error</dt>
          <dd>
            {t.hand_tracking.candidate_residual_px == null
              ? '—'
              : `${t.hand_tracking.candidate_residual_px.toFixed(1)} px`}
          </dd>
        </div>
        <div>
          <dt>Processing</dt>
          <dd>{ms(p.hand_inference_duration_ms)}</dd>
        </div>
        <div>
          <dt>Cached detections reused</dt>
          <dd>{p.inference_cache_hits ?? 0}</dd>
        </div>
        <div>
          <dt>Phone packet loss · total</dt>
          <dd>{transport?.packets_lost ?? '—'}</dd>
        </div>
        <div>
          <dt>Phone network jitter</dt>
          <dd>{ms(transport?.jitter_ms)}</dd>
        </div>
        <div>
          <dt>Phone clock estimate</dt>
          <dd>
            {transport?.clock_sync_ready === 1
              ? `±${ms(transport.clock_uncertainty_ms)}`
              : 'Waiting for samples'}
          </dd>
        </div>
        <div>
          <dt>Sender timestamps</dt>
          <dd>
            {(transport?.sender_reference_packets ?? 0) > 0
              ? 'Observed in RTP'
              : 'Not observed yet'}
          </dd>
        </div>
        <div>
          <dt>Round trip</dt>
          <dd>{ms(transport?.round_trip_ms)}</dd>
        </div>
        <div>
          <dt>Receiver jitter buffer · average</dt>
          <dd>{ms(transport?.jitter_buffer_ms)}</dd>
        </div>
      </dl>
      <p>
        Receive-time pairing is not verified capture synchronization. Missing transport counters
        appear as —.
      </p>
      {p.phone_time_offset_ms ? (
        <p>Active phone delay correction: {p.phone_time_offset_ms} ms.</p>
      ) : null}
      {p.phone_connection_event && <p>Phone connection: {p.phone_connection_event}</p>}
      <h3>Measure visual delay</h3>
      <p>
        Show one open palm to both cameras. Move it sideways at varying speeds, with a few brief
        pauses, for 20 seconds. Keep its depth and orientation steady. The estimate is a relative
        image delay, not a sensor synchronization measurement.
      </p>
      <button
        disabled={pending || phase !== 'idle' || !t.laptop.available || !t.phone.available}
        onClick={() => start('timing')}
      >
        Experimental: measure motion delay · 20 s
      </button>
      {estimate && (
        <p role="status">
          {estimate.status === 'collecting'
            ? 'Measuring palm motion…'
            : (estimate.reason ?? estimate.status)}
        </p>
      )}
      {estimate?.status === 'ready' && (
        <>
          <p>
            Estimated phone delay: {estimate.offset_ms} ms. Motion correlation:{' '}
            {estimate.correlation?.toFixed(3)}; without compensation:{' '}
            {estimate.baseline_correlation?.toFixed(3)}.
          </p>
          <button
            disabled={pending || phase !== 'idle' || !t.laptop.available || !t.phone.available}
            onClick={() => timing('apply')}
          >
            Apply and test movement · 20 s
          </button>
        </>
      )}
      {Boolean(p.phone_time_offset_ms) && (
        <button disabled={pending || phase !== 'idle'} onClick={() => timing('reset')}>
          Reset delay compensation
        </button>
      )}
      <p>
        After applying, repeat the same movement for 20 seconds. Compare measured samples and
        geometry error with the original moving-hand test. Compensation is not saved across server
        restarts.
      </p>
      <h3>Compare stationary and moving hands</h3>
      <p>
        Keep both cameras fixed. First hold one hand still in both views for 20 seconds. Then record
        20 seconds of slow side-to-side movement. Each test is labelled in the diagnostic log; no
        video is recorded.
      </p>
      <p role="status">
        Test: {phase === 'idle' ? 'Not recording a test' : `${phase} · recording for 20 seconds`}
      </p>
      <button
        disabled={pending || phase !== 'idle' || !t.laptop.available || !t.phone.available}
        onClick={() => start('stationary')}
      >
        Test stationary hand
      </button>
      <button
        disabled={pending || phase !== 'idle' || !t.laptop.available || !t.phone.available}
        onClick={() => start('moving')}
      >
        Test moving hand
      </button>
      {p.diagnostic_counts && (
        <p>
          Test samples:{' '}
          {Object.entries(p.diagnostic_counts)
            .map(([reason, count]) => `${reason.replaceAll('_', ' ')}: ${count}`)
            .join(' · ')}
        </p>
      )}
      {phase !== 'idle' && (
        <button disabled={pending} onClick={() => start('idle')}>
          Stop test
        </button>
      )}
    </section>
  )
}
