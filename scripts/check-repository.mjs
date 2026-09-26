import fs from 'node:fs'
import path from 'node:path'
import { execFileSync } from 'node:child_process'
import { pathToFileURL } from 'node:url'

const cyrillic = /\p{Script=Cyrillic}/u
const generated = /(?:^|\/)(?:node_modules|build(?:-[^/]+)?|cmake-build-[^/]+|deps|dist|coverage|\.nyc_output|\.venv|__pycache__|\.idea|\.vscode|logs|test-results|playwright-report)\//
const local = /(?:^|\/)(?:\.env(?:\..+)?|\.DS_Store|:memory:[^/]*)$|\.(?:pem|key|crt|p12|pfx|keystore|swp|swo|tsbuildinfo)$/i
const privateKey = /-----BEGIN (?:[A-Z0-9]+ )*PRIVATE KEY-----/

// A snapshot must be buildable and readable on its own: CI and a fresh clone only
// ever see these bytes, so a source file or linked document left out of the
// snapshot is a broken publication even while the working tree still builds.
const cmakeSource = /(?:src|tests|benchmarks)\/[\w./-]+\.(?:cpp|hpp)/g
const markdownLink = /\[[^\]]*\]\(([^)\s]+)\)/g

function checkSnapshotIntegrity(present, contents) {
  const failures = []
  const cmake = contents.get('backend/CMakeLists.txt')
  if (cmake) {
    for (const [reference] of new Set([...cmake.matchAll(cmakeSource)].map(m => [m[0]]))) {
      const file = `backend/${reference}`
      if (!present.has(file)) failures.push(`backend/CMakeLists.txt: builds ${reference}, absent from this snapshot`)
    }
  }
  const directories = new Set()
  for (const file of present) {
    for (let at = file.indexOf('/'); at !== -1; at = file.indexOf('/', at + 1)) directories.add(file.slice(0, at))
  }
  for (const [file, text] of contents) {
    if (!file.endsWith('.md')) continue
    for (const match of text.matchAll(markdownLink)) {
      const target = match[1]
      if (/^(?:[a-z][a-z0-9+.-]*:|#|\/\/)/i.test(target)) continue
      const relative = decodeURIComponent(target.split('#')[0].split('?')[0])
      if (!relative) continue
      const resolved = path.posix.normalize(path.posix.join(path.posix.dirname(file), relative)).replace(/\/$/, '')
      if (resolved.startsWith('..')) { failures.push(`${file}: link ${target} escapes the repository`); continue }
      if (!present.has(resolved) && !directories.has(resolved)) failures.push(`${file}: links ${target}, absent from this snapshot`)
    }
  }
  return failures
}

export function checkRepository(root, { staged = false } = {}) {
  const git = (...args) => execFileSync('git', args, { cwd: root, maxBuffer: 64 * 1024 * 1024 })
  const entries = staged
    ? git('ls-files', '--stage', '-z').toString().split('\0').filter(Boolean).map(entry => {
      const [metadata, ...name] = entry.split('\t')
      const [mode, object, stage] = metadata.split(' ')
      return { file: name.join('\t'), mode, object, stage }
    })
    : [...new Set(git('ls-files', '--cached', '--others', '--exclude-standard', '-z')
      .toString().split('\0').filter(Boolean))].map(file => ({ file }))
  const failures = []
  const present = new Set()
  const contents = new Map()
  for (const entry of entries) {
    const { file } = entry
    const fail = reason => failures.push(`${file}: ${reason}`)
    let buffer
    if (staged) {
      if (entry.stage !== '0') { fail('unresolved merge conflict'); continue }
      if (!['100644', '100755'].includes(entry.mode)) { fail('unsupported Git entry (symlink or submodule)'); continue }
      buffer = git('cat-file', 'blob', entry.object)
    } else {
      const absolute = path.join(root, file)
      let stat
      try { stat = fs.lstatSync(absolute) } catch (error) {
        if (error.code === 'ENOENT') continue // A tracked deletion in the working tree.
        throw error
      }
      if (stat.isSymbolicLink()) { fail('symlink must not expose files outside the repository'); continue }
      if (!stat.isFile()) continue
      buffer = fs.readFileSync(absolute)
    }
    if (cyrillic.test(file)) fail('Cyrillic filename')
    if (generated.test(file)) fail('generated dependency, build output, or local artifact')
    if (/\.py$/.test(file)) fail('retired application language')
    if ((local.test(file) && path.basename(file) !== '.env.example') ||
        /^(?:certs|models|data\/(?:calibration|captures|recordings|frames))\//.test(file)) {
      fail('local configuration, certificate, model weights, or capture data')
    }
    present.add(file)
    if (buffer.includes(0)) continue
    const text = buffer.toString('utf8')
    if (file.endsWith('.md') || file === 'backend/CMakeLists.txt') contents.set(file, text)
    if (cyrillic.test(text)) fail('non-English Cyrillic text')
    if (privateKey.test(text)) fail('private key material')
  }
  failures.push(...checkSnapshotIntegrity(present, contents))
  return failures
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const args = process.argv.slice(2)
  if (args.some(arg => arg !== '--staged')) {
    console.error('Usage: node scripts/check-repository.mjs [--staged]')
    process.exit(1)
  }
  const staged = args.includes('--staged')
  const failures = checkRepository(path.resolve(import.meta.dirname, '..'), { staged })
  if (failures.length) { console.error(failures.join('\n')); process.exit(1) }
  console.log(`Repository checks passed (${staged ? 'staged snapshot' : 'working tree'}): no Cyrillic text, application Python, private keys, or prohibited local artifacts; built sources and linked documents are present.`)
}
