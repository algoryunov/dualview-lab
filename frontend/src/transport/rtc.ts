import type { Telemetry } from '../types'
export type CameraId = 'laptop' | 'phone'
type VideoQuality = {
  width: number
  height: number
  fps: number
  bytes: number
  timestamp: number
  limitation: string
}
type Options = {
  role: 'dashboard' | 'phone'
  stream?: MediaStream
  session?: string | null
  onTelemetry?: (value: Telemetry) => void
  onTrack?: (camera: CameraId, stream: MediaStream) => void
  onState: (state: string) => void
  onError: (message: string) => void
}
export class RealtimeConnection {
  private peer: RTCPeerConnection
  private socket: WebSocket
  private channel?: RTCDataChannel
  private candidates: RTCIceCandidateInit[] = []
  private pending = new Map<
    string,
    { resolve: (value: unknown) => void; reject: (error: Error) => void; timer: number }
  >()
  private closed = false
  private statsTimer?: number
  private statsPending = false
  private diagnosticsSupported = false
  private setupTimer: number
  private options: Options
  constructor(options: Options) {
    this.options = options
    const peer = (this.peer = new RTCPeerConnection({ iceServers: [] }))
    const query = new URLSearchParams({ role: options.role })
    if (options.session) query.set('session', options.session)
    const socket = (this.socket = new WebSocket(
      `${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/ws/rtc?${query}`,
    ))
    this.setupTimer = window.setTimeout(() => this.fail('WebRTC connection timed out.'), 20000)
    peer.onconnectionstatechange = () => {
      if (this.closed) return
      if (peer.connectionState === 'failed' || peer.connectionState === 'disconnected') {
        this.phoneEvent(`connection_${peer.connectionState}`)
        this.fail('WebRTC connection interrupted.')
      } else if (peer.connectionState === 'connected') {
        this.reportReady()
      } else options.onState(peer.connectionState)
    }
    peer.onicecandidate = ({ candidate }) => {
      if (candidate && socket.readyState === WebSocket.OPEN)
        socket.send(
          JSON.stringify({
            type: 'candidate',
            candidate: candidate.candidate,
            sdpMLineIndex: candidate.sdpMLineIndex,
          }),
        )
    }
    peer.ontrack = (event) => {
      const index = peer.getTransceivers().findIndex((t) => t === event.transceiver)
      options.onTrack?.(index === 0 ? 'laptop' : 'phone', new MediaStream([event.track]))
    }
    if (options.role === 'dashboard') {
      for (let i = 0; i < 2; i++)
        this.preferVP8(peer.addTransceiver('video', { direction: 'recvonly' }))
      this.channel = peer.createDataChannel('control', { ordered: true })
      this.channel.onopen = () => this.reportReady()
      this.channel.onclose = () => this.fail('Realtime data channel closed.')
      this.channel.onerror = () => this.fail('Realtime data channel failed.')
      this.channel.onmessage = ({ data }) => {
        if (this.closed) return
        try {
          const message = JSON.parse(data)
          if (message.type === 'telemetry') options.onTelemetry?.(message.payload)
          if (message.type === 'response') {
            const request = this.pending.get(message.id)
            if (!request) return
            clearTimeout(request.timer)
            this.pending.delete(message.id)
            if (message.error) request.reject(new Error(message.error))
            else request.resolve(message.payload)
          }
        } catch {
          options.onError('Invalid realtime message.')
        }
      }
    } else {
      options.stream?.getTracks().forEach((track) => {
        peer.addTrack(track, options.stream!)
        track.addEventListener('ended', () => this.phoneEvent('camera_ended'), { once: true })
      })
      peer.getTransceivers().forEach((t) => this.preferVP8(t))
    }
    socket.onopen = () => {
      void (async () => {
        if (this.closed) return
        const offer = await peer.createOffer()
        await peer.setLocalDescription(offer)
        // Send SDP before asynchronous ICE candidates.
        socket.send(JSON.stringify({ type: 'offer', sdp: offer.sdp, clockSync: 1 }))
        for (const sender of peer.getSenders())
          if (sender.track?.kind === 'video') {
            const parameters = sender.getParameters()
            if (parameters.encodings[0]) {
              parameters.degradationPreference = 'maintain-resolution'
              parameters.encodings[0].scaleResolutionDownBy = 1
              parameters.encodings[0].maxBitrate = 6000000
              parameters.encodings[0].maxFramerate = 15
              await sender.setParameters(parameters)
            }
          }
      })().catch((error) => this.fail(String(error)))
    }
    let incoming = Promise.resolve()
    socket.onmessage = ({ data }) => {
      const received = Date.now()
      try {
        const ping = JSON.parse(data)
        if (ping.type === 'clock_ping' && options.role === 'phone') {
          if (!this.closed && socket.readyState === WebSocket.OPEN)
            socket.send(
              JSON.stringify({ type: 'clock_reply', id: ping.id, received, sent: Date.now() }),
            )
          return
        }
      } catch {
        /* Normal signaling below reports malformed messages. */
      }

      incoming = incoming
        .then(async () => {
          if (this.closed) return
          const message = JSON.parse(data)
          if (message.type === 'error') throw new Error(message.message)
          if (message.type === 'answer') {
            this.diagnosticsSupported = message.diagnostics === true
            await peer.setRemoteDescription({ type: 'answer', sdp: message.sdp })
            for (const candidate of this.candidates) await peer.addIceCandidate(candidate)
            this.candidates = []
          } else if (message.type === 'candidate') {
            const candidate = { candidate: message.candidate, sdpMLineIndex: message.sdpMLineIndex }
            if (peer.remoteDescription) await peer.addIceCandidate(candidate)
            else this.candidates.push(candidate)
          }
        })
        .catch((error) => this.fail(String(error)))
    }
    socket.onerror = () => this.fail('Could not reach the local server.')
    socket.onclose = () => {
      if (!this.closed) this.fail('Signaling connection closed.')
    }
  }
  private reportReady() {
    if (this.closed || this.peer.connectionState !== 'connected') return
    if (this.options.role === 'dashboard' && this.channel?.readyState !== 'open') return
    clearTimeout(this.setupTimer)
    this.options.onState('connected')
    if (this.options.role === 'phone' && this.diagnosticsSupported && this.statsTimer === undefined)
      this.statsTimer = window.setInterval(() => void this.reportPhoneStats(), 1000)
  }
  phoneEvent(reason: string) {
    if (
      !this.closed &&
      this.diagnosticsSupported &&
      this.options.role === 'phone' &&
      this.socket.readyState === WebSocket.OPEN
    )
      this.socket.send(JSON.stringify({ type: 'phone_event', reason }))
  }
  private async reportPhoneStats() {
    if (this.closed || this.statsPending) return
    this.statsPending = true
    try {
      const stats = await this.peer.getStats()
      const values: Record<string, number> = {}
      stats.forEach((r) => {
        if (r.type === 'remote-inbound-rtp' && r.kind === 'video') {
          if (Number.isFinite(r.roundTripTime)) values.round_trip_ms = r.roundTripTime * 1000
        }
        if (
          r.type === 'outbound-rtp' &&
          r.kind === 'video' &&
          r.framesEncoded > 0 &&
          Number.isFinite(r.totalEncodeTime)
        )
          values.phone_encode_ms = (r.totalEncodeTime * 1000) / r.framesEncoded
      })
      if (!this.closed && this.socket.readyState === WebSocket.OPEN)
        this.socket.send(JSON.stringify({ type: 'phone_stats', values }))
    } catch {
      /* Statistics are optional; never interrupt video for an unsupported counter. */
    } finally {
      this.statsPending = false
    }
  }
  private preferVP8(transceiver: RTCRtpTransceiver) {
    const available = RTCRtpReceiver.getCapabilities('video')?.codecs ?? []
    if (!available.some((codec) => codec.mimeType.toLowerCase() === 'video/vp8')) return
    // Keep retransmission support when restricting the primary codec to VP8.
    transceiver.setCodecPreferences(
      available.filter((codec) =>
        ['video/vp8', 'video/rtx'].includes(codec.mimeType.toLowerCase()),
      ),
    )
  }
  private fail(message: string) {
    if (this.closed) return
    this.options.onError(message)
    this.close()
    this.options.onState('disconnected')
  }
  command(command: string, payload: Record<string, unknown> = {}) {
    if (this.closed || this.channel?.readyState !== 'open')
      return Promise.reject(new Error('Realtime connection is not ready.'))
    const id = crypto.randomUUID()
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.pending.delete(id)
        reject(
          new Error(
            'Command timed out; outcome may be unknown. Check the current state before retrying.',
          ),
        )
      }, 10000)
      this.pending.set(id, { resolve, reject, timer })
      try {
        this.channel!.send(JSON.stringify({ ...payload, id, command }))
      } catch (error) {
        clearTimeout(timer)
        this.pending.delete(id)
        reject(error)
      }
    })
  }
  async videoStats(): Promise<VideoQuality | null> {
    if (this.closed) return null
    const stats = await this.peer.getStats()
    let video: VideoQuality | null = null
    stats.forEach((report) => {
      if (report.type === 'outbound-rtp' && report.kind === 'video' && !report.isRemote) {
        video = {
          width: report.frameWidth ?? 0,
          height: report.frameHeight ?? 0,
          fps: report.framesPerSecond ?? 0,
          bytes: report.bytesSent ?? 0,
          timestamp: report.timestamp,
          limitation: report.qualityLimitationReason ?? 'unknown',
        }
      }
    })
    return video
  }
  close() {
    if (this.closed) return
    this.closed = true
    clearTimeout(this.setupTimer)
    clearInterval(this.statsTimer)
    for (const request of this.pending.values()) {
      clearTimeout(request.timer)
      request.reject(new Error('Connection closed.'))
    }
    this.pending.clear()
    this.channel?.close()
    this.peer.close()
    this.socket.close()
  }
}
