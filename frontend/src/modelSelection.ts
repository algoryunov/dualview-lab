import { useSyncExternalStore } from 'react'
export const demoModels = [
  { id: 'car', label: 'Car', icon: '🚙' },
  { id: 'bed', label: 'Bed', icon: '🛏' },
  { id: 'flower', label: 'Flower', icon: '🌸' },
  { id: 'tower', label: 'Tower', icon: '♜' },
] as const
let selected = 0
const listeners = new Set<() => void>()
export function selectModel(index: number) {
  selected = (index + demoModels.length) % demoModels.length
  listeners.forEach((listener) => listener())
}
export function nextModel() {
  selectModel(selected + 1)
}
export function useModelSelection() {
  return useSyncExternalStore(
    (listener) => {
      listeners.add(listener)
      return () => {
        listeners.delete(listener)
      }
    },
    () => selected,
  )
}

// Consume backend dwell progress only on fresh telemetry, once per released hold.
export class ModelGestureLatch {
  private fired = false
  update(gesture: string | null, elapsed: number, valid: boolean) {
    if (gesture !== 'Pointing_Up' && gesture !== 'pointing_up') {
      this.fired = false
      return false
    }
    if (!valid) {
      this.fired = true
      return false
    }
    if (elapsed < 900 || this.fired) return false
    this.fired = true
    return true
  }
}
