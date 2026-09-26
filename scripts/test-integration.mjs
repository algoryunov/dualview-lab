import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { createServer } from 'node:net'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'

const root = path.resolve(import.meta.dirname, '..')
const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'dualview-integration-'))
const probe = createServer()
probe.listen(0, '127.0.0.1')
await once(probe, 'listening')
const port = probe.address().port
await new Promise(resolve => probe.close(resolve))
const origin = `http://localhost:${port}`
const provider = process.env.TEST_PROVIDER

function run(file, env) {
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [file], { cwd: root, env, stdio: 'inherit' })
    child.once('error', reject)
    child.once('exit', (code, signal) => code === 0 ? resolve() : reject(new Error(`${file} failed: ${code ?? signal}`)))
  })
}
async function stop(child) {
  if (!child.pid || child.exitCode !== null || child.signalCode !== null) return
  const exited = once(child, 'exit')
  child.kill('SIGTERM')
  const timer = setTimeout(() => child.kill('SIGKILL'), 5000)
  try { await exited } finally { clearTimeout(timer) }
}

try {
  for (const showQR of [true, false]) {
    const env = {
      ...process.env,
      DUALVIEW_HOST: '127.0.0.1', DUALVIEW_PORT: String(port), DUALVIEW_TLS: 'false',
      DUALVIEW_PUBLIC_ORIGIN: origin, DUALVIEW_SHOW_QR_CODE: String(showQR),
      DUALVIEW_PAIRING_REQUIRED: 'true', DUALVIEW_CAMERA_ENABLED: 'false',
      DUALVIEW_INFERENCE_ENABLED: String(Boolean(provider)),
      DUALVIEW_INFERENCE_PROVIDER: provider || 'cpu',
      DUALVIEW_CALIBRATION_FILE: path.join(directory, 'calibration.yml'),
      DUALVIEW_TRACKING_LOG: path.join(directory, 'tracking.jsonl'),
      DUALVIEW_METAL_LOG: path.join(directory, 'metal.jsonl'),
      TEST_ORIGIN: origin, EXPECT_QR: String(showQR), EXPECT_PROVIDER: provider || 'disabled',
    }
    const server = spawn('bash', ['-c', 'source scripts/gstreamer-env.sh\nexec backend/build/dualview_server'], { cwd: root, env, stdio: ['ignore', 'pipe', 'pipe'] })
    let logs = '', startupError
    server.on('error', error => { startupError = error })
    for (const stream of [server.stdout, server.stderr]) stream.on('data', data => { logs = (logs + data).slice(-40000) })
    try {
      let ready = false
      for (let attempt = 0; attempt < 100; attempt++) {
        if (startupError) throw startupError
        if (server.exitCode !== null || server.signalCode !== null) throw new Error('Native test server exited during startup')
        try { ready = (await fetch(`${origin}/api/health`, { signal: AbortSignal.timeout(500) })).ok } catch { /* Wait for the listener. */ }
        if (ready) break
        await new Promise(resolve => setTimeout(resolve, 200))
      }
      if (!ready) throw new Error('Native test server did not become ready')
      console.log(`Integration: QR ${showQR ? 'shown' : 'hidden'}, provider ${provider || 'disabled'}`)
      await run('frontend/tests/browser-smoke.mjs', env)
      const samples = (await fs.readFile(env.DUALVIEW_METAL_LOG, 'utf8')).trim().split('\n').map(line => JSON.parse(line))
      if (!samples.some(sample => sample.event === 'metal_sample' && sample.blobs.length === 20 && sample.velocities.length === 15)) {
        throw new Error('Rendered metal diagnostics did not reach the native log')
      }
      console.log('PASS: rendered metal samples persisted through the WebRTC DataChannel')
      await run('frontend/tests/pairing-security.mjs', env)
    } catch (error) {
      console.error(logs)
      throw error
    } finally { await stop(server) }
  }
} finally { await fs.rm(directory, { recursive: true, force: true }) }
