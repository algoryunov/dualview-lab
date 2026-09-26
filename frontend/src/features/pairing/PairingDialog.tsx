import { useCallback, useEffect, useRef, useState } from 'react'

type Session = { phone_url: string; expires_in_seconds: number }
type Pairing = { session: Session; qr: string; expiresAt: number | null }

export function PairingDialog({ onClose }: { onClose: () => void }) {
  const dialog = useRef<HTMLDialogElement>(null)
  const request = useRef<AbortController | null>(null)
  const [pairing, setPairing] = useState<Pairing | null>(null)
  const [pending, setPending] = useState(true)
  const [error, setError] = useState('')
  const [now, setNow] = useState(Date.now())
  const expired = pairing?.expiresAt != null && now >= pairing.expiresAt

  const refresh = useCallback(async () => {
    request.current?.abort()
    const controller = new AbortController()
    request.current = controller
    const { signal } = controller
    setPending(true)
    setError('')
    setPairing(null)
    try {
      const configResponse = await fetch('/api/config', { signal })
      if (!configResponse.ok) throw new Error('Could not load pairing settings.')
      const config = (await configResponse.json()) as { show_qr_code: boolean }
      const response = await fetch('/api/session', { method: 'POST', signal })
      if (!response.ok) throw new Error('Could not create a pairing session.')
      const session = (await response.json()) as Session
      const created = Date.now()
      let qr = ''
      if (config.show_qr_code) {
        const { default: QRCode } = await import('qrcode')
        if (signal.aborted) return
        qr = await QRCode.toDataURL(session.phone_url, { width: 280, margin: 4 })
      }
      if (!signal.aborted) {
        setNow(created)
        setPairing({
          session,
          qr,
          expiresAt: session.expires_in_seconds
            ? created + session.expires_in_seconds * 1000
            : null,
        })
      }
    } catch (error) {
      if (!signal.aborted) setError(error instanceof Error ? error.message : String(error))
    } finally {
      if (!signal.aborted) setPending(false)
    }
  }, [])

  useEffect(() => {
    dialog.current?.showModal()
    void refresh()
    const timer = window.setInterval(() => setNow(Date.now()), 1000)
    return () => {
      request.current?.abort()
      window.clearInterval(timer)
    }
  }, [refresh])

  return (
    <dialog ref={dialog} onCancel={onClose} aria-label="Connect phone">
      <div className="dialog-heading">
        <h2>Connect your phone</h2>
        <button onClick={onClose} aria-label="Close pairing">
          ✕
        </button>
      </div>
      <p>Open this link on a phone on the same Wi-Fi network, then start the camera.</p>
      {pending && <p role="status">Creating connection link…</p>}
      {error && <p role="alert">{error}</p>}
      {expired ? (
        <p role="status">This connection link has expired. Refresh it to connect a phone.</p>
      ) : (
        pairing && (
          <>
            {pairing.qr && <img src={pairing.qr} alt="Phone pairing QR code" />}
            <a className="pairing-link" href={pairing.session.phone_url}>
              {pairing.session.phone_url}
            </a>
            <p>
              {pairing.expiresAt
                ? `Link expires at ${new Date(pairing.expiresAt).toLocaleTimeString('en-GB')}.`
                : 'Direct connection is enabled.'}
            </p>
          </>
        )
      )}
      <button disabled={pending} onClick={() => void refresh()}>
        Refresh connection link
      </button>
    </dialog>
  )
}
