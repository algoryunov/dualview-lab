import { useSyncExternalStore } from 'react'
type Settings = { finish: number; occlusion: boolean; input: 'hands' | 'auto' }
let settings: Settings = { finish: 0, occlusion: true, input: 'hands' }
const listeners = new Set<() => void>()
export function changeMetalSettings(patch: Partial<Settings>) {
  settings = { ...settings, ...patch }
  listeners.forEach((fn) => fn())
}
export function useMetalSettings() {
  return useSyncExternalStore(
    (fn) => {
      listeners.add(fn)
      return () => {
        listeners.delete(fn)
      }
    },
    () => settings,
  )
}
