import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { execFileSync } from 'node:child_process'
import { test } from 'node:test'
import { checkRepository } from '../check-repository.mjs'

function repository(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'dualview-repository-'))
  t.after(() => fs.rmSync(root, { recursive: true, force: true }))
  const git = (...args) => execFileSync('git', args, { cwd: root, stdio: 'pipe' })
  git('init')
  const write = (file, text) => {
    fs.mkdirSync(path.dirname(path.join(root, file)), { recursive: true })
    fs.writeFileSync(path.join(root, file), text)
  }
  return { root, git, write }
}

test('staged checks inspect the committed bytes even after a local fix or deletion', t => {
  const { root, git, write } = repository(t)
  write('README.md', '\u0422\u0435\u0441\u0442')
  write('old.py', '# Retired backend')
  git('add', '.')
  write('README.md', 'English documentation')
  fs.unlinkSync(path.join(root, 'old.py'))
  assert.deepEqual(checkRepository(root), [])
  const failures = checkRepository(root, { staged: true })
  assert.ok(failures.some(value => value.includes('Cyrillic text')))
  assert.ok(failures.some(value => value.includes('retired application language')))
})

test('forced additions of ignored local files are rejected, examples and demo assets remain allowed', t => {
  const { root, git, write } = repository(t)
  write('.gitignore', '.env*\n!.env.example\nbackend/build-debug/\n')
  write('.env.production', 'PASSWORD=example')
  write('.env.example', 'HOST=localhost')
  write('backend/build-debug/output', 'generated')
  write('frontend/public/models/demo.glb', Buffer.from([0, 1, 2]))
  git('add', '.')
  git('add', '-f', '.env.production', 'backend/build-debug/output')
  const failures = checkRepository(root, { staged: true })
  assert.equal(failures.length, 2)
  assert.ok(failures.some(value => value.startsWith('.env.production:')))
  assert.ok(failures.some(value => value.startsWith('backend/build-debug/output:')))
})

test('private keys and Cyrillic filenames are detected', t => {
  const { root, write } = repository(t)
  write('notes.txt', ['-----BEGIN', 'ENCRYPTED PRIVATE KEY-----'].join(' '))
  write('\u0442\u0435\u0441\u0442.txt', 'English')
  const failures = checkRepository(root)
  assert.ok(failures.some(value => value.includes('private key material')))
  assert.ok(failures.some(value => value.includes('Cyrillic filename')))
})

test('symlinks are rejected without reading their targets', t => {
  const { root, git } = repository(t)
  fs.symlinkSync('/missing-private-file', path.join(root, 'link'))
  assert.match(checkRepository(root)[0], /symlink/)
  git('add', '.')
  assert.match(checkRepository(root, { staged: true })[0], /symlink/)
})

test('a snapshot missing a built source or a linked document is rejected', t => {
  const { root, git, write } = repository(t)
  write('backend/CMakeLists.txt', [
    'add_library(dualview_runtime src/runtime.cpp src/runtime_protocol.cpp)',
    'add_executable(geometry_evaluation benchmarks/geometry_evaluation.cpp)',
  ].join('\n'))
  write('backend/src/runtime.cpp', 'int main() { return 0; }')
  write('README.md', 'See [evaluation](docs/evaluation.md) and [license](LICENSE).')
  write('LICENSE', 'MIT')
  git('add', '.')
  // runtime_protocol.cpp, geometry_evaluation.cpp and docs/evaluation.md exist
  // nowhere: the snapshot compiles and reads only by accident of a local tree.
  const failures = checkRepository(root, { staged: true })
  assert.ok(failures.some(value => value.includes('builds src/runtime_protocol.cpp')))
  assert.ok(failures.some(value => value.includes('builds benchmarks/geometry_evaluation.cpp')))
  assert.ok(failures.some(value => value.includes('links docs/evaluation.md')))
  assert.ok(!failures.some(value => value.includes('LICENSE')))
})

test('a complete snapshot accepts directory, anchor, and external links', t => {
  const { root, git, write } = repository(t)
  write('backend/CMakeLists.txt', 'add_library(dualview_runtime src/runtime.cpp)')
  write('backend/src/runtime.cpp', 'int main() { return 0; }')
  write('docs/evaluation/results.json', '{}')
  write('docs/guide.md', '# Guide')
  write('README.md', [
    '[guide](docs/guide.md) [raw](docs/evaluation/results.json) [dir](docs/evaluation)',
    '[anchor](#scope) [site](https://example.com) [source](backend/src/runtime.cpp)',
  ].join('\n'))
  git('add', '.')
  assert.deepEqual(checkRepository(root, { staged: true }), [])
})

test('a document link escaping the repository is rejected', t => {
  const { root, git, write } = repository(t)
  write('docs/guide.md', 'See [secrets](../../etc/passwd).')
  git('add', '.')
  const failures = checkRepository(root, { staged: true })
  assert.ok(failures.some(value => value.includes('escapes the repository')))
})
