import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
const root = path.resolve(import.meta.dirname, '..')
const mode = process.argv[2]
if (!['--write', '--check'].includes(mode)) throw new Error('Usage: node scripts/format-native.mjs --write|--check')
function files(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
    const file = path.join(directory, entry.name)
    return entry.isDirectory() ? files(file) : /\.(cpp|hpp)$/.test(entry.name) ? [file] : []
  })
}
const sources = ['include', 'src', 'tests', 'benchmarks'].flatMap(directory => files(path.join(root, 'backend', directory)))
const result = spawnSync('clang-format', [...(mode === '--write' ? ['-i'] : ['--dry-run', '--Werror']), ...sources], { cwd: root, stdio: 'inherit' })
if (result.error) throw new Error(`clang-format is required: ${result.error.message}`)
process.exit(result.status ?? 1)
