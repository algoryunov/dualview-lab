import { TrackingDiagnostics } from './TrackingDiagnostics'
import { Camera } from './Camera'
import { changeMetalSettings, useMetalSettings } from '../../metalSettings'
import type { CameraId } from '../../transport/rtc'
import type { Telemetry } from '../../types'
type Props = {
  onConnectPhone: () => void
  telemetry: Telemetry
  streams: Partial<Record<CameraId, MediaStream>>
  status: string
  pending: boolean
  run: (action: () => Promise<void>) => Promise<void>
  command: (name: string, payload?: Record<string, unknown>) => Promise<void>
}
export function SetupPanel({
  telemetry: t,
  streams,
  status,
  pending,
  run,
  command,
  onConnectPhone,
}: Props) {
  const metal = useMetalSettings()
  const busy = t.intrinsics.status === 'capturing' || t.calibration.status === 'capturing'
  return (
    <section className="setup">
      <div className="setup-heading">
        <h1>Settings</h1>
        <button onClick={onConnectPhone}>Connect phone</button>
      </div>
      <p>
        Calibrate each camera, then the fixed pair. Keep the cameras in position during interaction.
      </p>
      <div className="camera-grid">
        <Camera id="laptop" stream={streams.laptop} telemetry={t} />
        <Camera id="phone" stream={streams.phone} telemetry={t} />
      </div>
      <div className="setup-grid">
        <section>
          <h2>1. Camera profiles</h2>
          <p>{t.intrinsics.message}</p>
          {(['laptop', 'phone'] as const).map((camera) => {
            const profile = t.intrinsics.profiles?.[camera]
            return (
              <p key={camera}>
                <strong>{camera === 'laptop' ? 'Laptop' : 'Phone'}: </strong>
                {profile?.available
                  ? `Saved profile · ${profile.width} × ${profile.height}`
                  : 'No saved profile'}
                {profile?.compatibility === 'waiting_for_camera' && ' · Waiting for camera'}
                {profile?.compatibility === 'compatible' && ' · Resolution compatible'}
                {profile?.compatibility === 'mode_mismatch' &&
                  ` · Recalibrate for this camera mode. ${profile.detail}`}
              </p>
            )
          })}
          <p>
            {t.intrinsics.accepted_views} / {t.intrinsics.required_views} views ·{' '}
            {t.intrinsics.status}
          </p>
          <progress
            value={t.intrinsics.accepted_views}
            max={t.intrinsics.required_views}
            aria-label="Camera profile progress"
          />
          <p>
            25 distinct target views, sampled at most twice per second. Move and tilt the target.
            Capture continues until enough valid views are accepted or you cancel.
          </p>
          <p>
            {Math.floor((t.intrinsics.elapsed_ms ?? 0) / 1000)} s elapsed ·{' '}
            {t.intrinsics.rejected_views} rejected
          </p>
          {t.intrinsics.last_rejection && <p role="status">{t.intrinsics.last_rejection}</p>}
          {(['laptop', 'phone'] as const).map((camera) => (
            <button
              key={camera}
              disabled={pending || busy || !t[camera].available}
              onClick={() => void run(() => command('intrinsics.start', { camera }))}
            >
              Calibrate {camera}
            </button>
          ))}
        </section>
        <section>
          <h2>2. Stereo calibration</h2>
          <p>{t.calibration.message}</p>
          <p>
            {t.calibration.accepted_views} / {t.calibration.required_views ?? 20} pairs ·{' '}
            {t.calibration.status}
          </p>
          <progress
            value={t.calibration.accepted_views}
            max={t.calibration.required_views ?? 20}
            aria-label="Stereo calibration progress"
          />
          <p>
            20 valid frame pairs with the target visible in both cameras and timing within 80 ms. At
            most two pairs per second; rejected pairs extend capture time. Keep the cameras fixed
            and hold the target still for each sample.
          </p>
          <p>
            {Math.floor((t.calibration.elapsed_ms ?? 0) / 1000)} s elapsed ·{' '}
            {t.calibration.rejected_views} rejected
          </p>
          {t.calibration.last_rejection && <p role="status">{t.calibration.last_rejection}</p>}
          <button
            disabled={pending || busy || !t.laptop.available || !t.phone.available}
            onClick={() => void run(() => command('calibration.start'))}
          >
            Calibrate pair
          </button>
          {busy && (
            <button
              disabled={pending}
              onClick={() => void run(() => command('calibration.cancel'))}
            >
              Cancel capture
            </button>
          )}
        </section>
      </div>
      <TrackingDiagnostics
        telemetry={t}
        timing={(action) => void run(() => command(`timing.${action}`))}
        pending={pending}
        start={(phase) => void run(() => command('diagnostics.phase', { phase }))}
      />
      <details>
        <summary>Diagnostics and interaction</summary>
        <p>Pinch: move · two pinches: scale · turn your palm while pinching: rotate.</p>
        <button onClick={() => void run(() => command('interaction.reset'))}>Reset object</button>
        <label>
          <input
            type="checkbox"
            checked={metal.occlusion}
            onChange={(event) => changeMetalSettings({ occlusion: event.target.checked })}
          />{' '}
          Approximate hand occlusion
        </label>
        <pre>
          {JSON.stringify(
            {
              connection: status,
              processing: t.processing,
              tracking: t.hand_tracking.status,
              reason: t.hand_tracking.reason,
              calibration: t.calibration,
              profiles: t.intrinsics.profiles,
            },
            null,
            2,
          )}
        </pre>
      </details>
    </section>
  )
}
