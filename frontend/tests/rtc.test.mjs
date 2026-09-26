import test from 'node:test'
import assert from 'node:assert/strict'
import { RealtimeConnection } from '../src/transport/rtc.ts'

function fixture(t, codecs = [], role = 'dashboard') {
  let peer, socket, interval
  const messages = []
  class FakeSocket {
    static OPEN = 1
    readyState = 1
    close() {
      this.readyState = 3
    }
    constructor() {
      socket = this
    }
    send(text) {
      messages.push(JSON.parse(text))
    }
  }
  class FakePeer {
    preferences = []
    connectionState = 'new'
    channel = { readyState: 'connecting', close() {}, send() {} }
    constructor() {
      peer = this
    }
    addTransceiver() {
      return { setCodecPreferences: (codecs) => this.preferences.push(codecs) }
    }
    async setRemoteDescription() {}
    getTransceivers() {
      return []
    }
    createDataChannel() {
      return this.channel
    }
    close() {
      this.connectionState = 'closed'
    }
  }
  for (const [key, value] of Object.entries({
    RTCPeerConnection: FakePeer,
    WebSocket: FakeSocket,
    RTCRtpReceiver: { getCapabilities: () => ({ codecs }) },
    location: { protocol: 'https:', host: 'localhost' },
    window: {
      setTimeout,
      setInterval: (callback) => {
        interval = callback
        return 123
      },
    },
  })) {
    const previous = Object.getOwnPropertyDescriptor(globalThis, key)
    Object.defineProperty(globalThis, key, { value, configurable: true, writable: true })
    t.after(() => {
      if (previous) Object.defineProperty(globalThis, key, previous)
      else delete globalThis[key]
    })
  }
  const states = [],
    errors = []
  const connection = new RealtimeConnection({
    role,
    onState: (state) => states.push(state),
    onError: (error) => errors.push(error),
  })
  t.after(() => connection.close())
  return { connection, peer, states, errors, socket, messages, tick: () => interval() }
}

test('VP8 preference retains retransmission codecs for lossy networks', (t) => {
  const vp8 = { mimeType: 'video/VP8', clockRate: 90000 }
  const rtx = { mimeType: 'video/rtx', clockRate: 90000 }
  const vp9 = { mimeType: 'video/VP9', clockRate: 90000 }
  const { peer } = fixture(t, [vp8, rtx, vp9])
  assert.deepEqual(peer.preferences, [
    [vp8, rtx],
    [vp8, rtx],
  ])
})

test('dashboard is ready only when both peer and DataChannel are ready', async (t) => {
  const { connection, peer, states } = fixture(t)
  peer.connectionState = 'connected'
  peer.onconnectionstatechange()
  assert.deepEqual(states, [])
  await assert.rejects(connection.command('interaction.reset'), /not ready/)
  peer.channel.readyState = 'open'
  peer.channel.onopen()
  assert.deepEqual(states, ['connected'])
})

test('a transport failure emits one disconnect and rejects pending commands', async (t) => {
  const { connection, peer, states, errors } = fixture(t)
  peer.channel.readyState = 'open'
  const pending = connection.command('interaction.reset')
  peer.connectionState = 'disconnected'
  peer.onconnectionstatechange()
  await assert.rejects(pending, /Connection closed/)
  peer.channel.onclose()
  assert.deepEqual(states, ['disconnected'])
  assert.equal(errors.length, 1)
})

test('a synchronous DataChannel send failure rejects the command', async (t) => {
  const { connection, peer } = fixture(t)
  peer.channel.readyState = 'open'
  peer.channel.send = () => {
    throw new Error('send failed')
  }
  await assert.rejects(connection.command('interaction.reset'), /send failed/)
})

test('video diagnostics report the actual outbound stream and stop after close', async (t) => {
  const { connection, peer } = fixture(t)
  peer.getStats = async () =>
    new Map([
      ['audio', { type: 'outbound-rtp', kind: 'audio' }],
      [
        'video',
        {
          type: 'outbound-rtp',
          kind: 'video',
          frameWidth: 1280,
          frameHeight: 720,
          framesPerSecond: 15,
          bytesSent: 100000,
          timestamp: 2000,
          qualityLimitationReason: 'bandwidth',
        },
      ],
    ])
  assert.deepEqual(await connection.videoStats(), {
    width: 1280,
    height: 720,
    fps: 15,
    bytes: 100000,
    timestamp: 2000,
    limitation: 'bandwidth',
  })
  connection.close()
  assert.equal(await connection.videoStats(), null)
})

test('phone reports optional timing counters and stops diagnostics after close', async (t) => {
  const { connection, peer, socket, messages, tick } = fixture(t, [], 'phone')
  socket.onmessage({ data: JSON.stringify({ type: 'answer', sdp: 'test', diagnostics: true }) })
  await new Promise((resolve) => setImmediate(resolve))
  peer.getStats = async () =>
    new Map([
      ['remote', { type: 'remote-inbound-rtp', kind: 'video', roundTripTime: 0.04 }],
      ['out', { type: 'outbound-rtp', kind: 'video', framesEncoded: 20, totalEncodeTime: 0.1 }],
    ])
  peer.connectionState = 'connected'
  peer.onconnectionstatechange()
  tick()
  await new Promise((resolve) => setImmediate(resolve))
  assert.deepEqual(messages.at(-1), {
    type: 'phone_stats',
    values: { round_trip_ms: 40, phone_encode_ms: 5 },
  })
  connection.phoneEvent('user_stop')
  assert.equal(messages.at(-1).reason, 'user_stop')
  const count = messages.length
  connection.close()
  tick()
  await new Promise((resolve) => setImmediate(resolve))
  assert.equal(messages.length, count)
})

test('older servers do not receive unsupported diagnostic messages', async (t) => {
  const { connection, peer, messages } = fixture(t, [], 'phone')
  peer.connectionState = 'connected'
  peer.onconnectionstatechange()
  connection.phoneEvent('user_stop')
  assert.equal(messages.length, 0)
})

test('phone clock replies bypass pending SDP work and stop on close', async (t) => {
  const { connection, peer, socket, messages } = fixture(t, [], 'phone')
  let release
  peer.setRemoteDescription = () =>
    new Promise((resolve) => {
      release = resolve
    })
  socket.onmessage({ data: JSON.stringify({ type: 'answer', sdp: 'test' }) })
  await new Promise((resolve) => setImmediate(resolve))
  socket.onmessage({ data: JSON.stringify({ type: 'clock_ping', id: 42 }) })
  assert.equal(messages.at(-1).type, 'clock_reply')
  assert.equal(messages.at(-1).id, 42)
  assert.ok(messages.at(-1).sent >= messages.at(-1).received)
  connection.close()
  const count = messages.length
  socket.onmessage({ data: JSON.stringify({ type: 'clock_ping', id: 43 }) })
  assert.equal(messages.length, count)
  release()
})
