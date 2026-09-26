import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
const root = path.resolve(import.meta.dirname, '..')
const report = JSON.parse(execFileSync(path.join(root, 'backend/build/geometry_evaluation'), ['--check'], { encoding: 'utf8' }))
report.environment = {
  recorded_at: new Date().toISOString(),
  os: os.type(), release: os.release(), architecture: os.arch(),
  cpu: os.cpus()[0]?.model ?? 'unknown', logical_cpus: os.cpus().length,
  memory_bytes: os.totalmem(),
  timing_note: 'Single local run; no CPU isolation. Not an end-to-end latency benchmark.',
}
const text = JSON.stringify(report, null, 2) + '\n'
if (process.argv[2]) fs.writeFileSync(process.argv[2], text)
else process.stdout.write(text)
