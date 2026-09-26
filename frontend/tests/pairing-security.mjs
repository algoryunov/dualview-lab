import assert from 'node:assert/strict'
const origin = process.env.TEST_ORIGIN || 'http://localhost:18445'
const response = await fetch(`${origin}/api/session`, {
  method: 'POST',
  headers: { Origin: 'https://untrusted.example' },
})
assert.equal(response.status, 403)
const session = await (await fetch(`${origin}/api/session`, { method: 'POST' })).json()
const oldToken = new URL(session.phone_url).searchParams.get('session')
await fetch(`${origin}/api/session`, { method: 'POST' })
async function rejected(token) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(
      `${origin.replace(/^http/, 'ws')}/ws/rtc?role=phone&session=${encodeURIComponent(token)}`,
    )
    const timeout = setTimeout(() => {
      ws.close()
      reject(new Error('Pairing rejection timed out'))
    }, 5000)
    ws.onclose = (e) => {
      clearTimeout(timeout)
      resolve(e.code)
    }
    ws.onerror = () => {}
  })
}
assert.equal(await rejected('invalid'), 1008)
assert.equal(await rejected(oldToken), 1008)
console.log('PASS: cross-origin mutation, invalid token, and invalidated token rejected')
