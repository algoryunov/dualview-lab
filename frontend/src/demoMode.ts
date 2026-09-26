import { useSyncExternalStore } from 'react'
export const effectModes = [
  {
    id: 'metal',
    label: 'Liquid Metal',
    hint: 'Spread your palms to stretch the metal. Bring them together to merge it.',
  },
  {
    id: 'models',
    label: '3D Models',
    hint: 'Explore the model collection with hand gestures or your mouse.',
  },
  {
    id: 'trails',
    label: 'Light Trails',
    hint: 'Draw in the air with your fingertips. Trails fade smoothly.',
  },
  { id: 'energy', label: 'Energy', hint: 'Hold two palms apart to shape a ball of energy.' },
] as const
export type DemoMode = (typeof effectModes)[number]['id']
let mode: DemoMode = 'metal'
const listeners = new Set<() => void>()
export function setDemoMode(next: DemoMode) {
  mode = next
  listeners.forEach((fn) => fn())
}
export function useDemoMode() {
  return useSyncExternalStore(
    (fn) => {
      listeners.add(fn)
      return () => {
        listeners.delete(fn)
      }
    },
    () => mode,
  )
}

export function validHands(hands: number[][][]) {
  return hands
    .filter(
      (hand) =>
        hand.length === 21 &&
        hand.every((p) => p.length >= 2 && Number.isFinite(p[0]) && Number.isFinite(p[1])),
    )
    .slice(0, 2)
}
export function fitVideo(width: number, height: number, videoWidth: number, videoHeight: number) {
  const scale = Math.min(width / Math.max(1, videoWidth), height / Math.max(1, videoHeight))
  return {
    width: videoWidth * scale,
    height: videoHeight * scale,
    left: (width - videoWidth * scale) / 2,
    top: (height - videoHeight * scale) / 2,
  }
}
