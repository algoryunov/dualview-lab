import { handConnections as connections } from './handConnections'
import { DemoEffects } from './DemoEffects'
import { useDemoMode } from './demoMode'
import { useEffect, useRef } from 'react'
import type { StreamHealth } from './types'

const cubeEdges: Array<[number, number]> = [
  [0, 1],
  [1, 2],
  [2, 3],
  [3, 0],
  [4, 5],
  [5, 6],
  [6, 7],
  [7, 4],
  [0, 4],
  [1, 5],
  [2, 6],
  [3, 7],
]

function DiagnosticOverlay({
  hands,
  cube,
  cubeSegments,
  objectPoints,
  handDepthRelation,
  health,
  tracked,
}: {
  cameraId?: 'laptop' | 'phone'
  hands: number[][][]
  cube: number[][] | null
  cubeSegments?: number[][][] | null
  objectPoints?: number[][] | null
  handDepthRelation?: string
  health: StreamHealth
  tracked: boolean
}) {
  const canvas = useRef<HTMLCanvasElement>(null)
  useEffect(() => {
    const element = canvas.current
    if (!element) return
    const draw = () => {
      const { width, height } = element.getBoundingClientRect()
      const ratio = Math.min(window.devicePixelRatio, 2)
      element.width = Math.max(1, Math.round(width * ratio))
      element.height = Math.max(1, Math.round(height * ratio))
      const context = element.getContext('2d')
      if (!context || !health.width || !health.height) return
      context.setTransform(ratio, 0, 0, ratio, 0, 0)
      const scale = Math.min(width / health.width, height / health.height)
      const imageWidth = health.width * scale
      const imageHeight = health.height * scale
      const left = (width - imageWidth) / 2
      const top = (height - imageHeight) / 2
      const at = (point: number[]) =>
        [left + point[0] * imageWidth, top + point[1] * imageHeight] as const
      const drawObject = () => {
        context.strokeStyle = '#a894ff'
        context.lineWidth = 2
        if (objectPoints) {
          context.fillStyle = '#ffd27a'
          objectPoints.forEach((point) => {
            const [x, y] = at(point)
            context.beginPath()
            context.arc(x, y, 1.7, 0, Math.PI * 2)
            context.fill()
          })
        } else if (cubeSegments) {
          cubeSegments.forEach(([from, to]) => {
            if (!from || !to) return
            const [x1, y1] = at(from)
            const [x2, y2] = at(to)
            context.beginPath()
            context.moveTo(x1, y1)
            context.lineTo(x2, y2)
            context.stroke()
          })
        } else if (cube?.length === 8) {
          cubeEdges.forEach(([from, to]) => {
            const [x1, y1] = at(cube[from])
            const [x2, y2] = at(cube[to])
            context.beginPath()
            context.moveTo(x1, y1)
            context.lineTo(x2, y2)
            context.stroke()
          })
        }
      }
      const drawHands = () =>
        hands
          .filter((points) => points.length === 21)
          .forEach((points, handIndex) => {
            const palette =
              handIndex === 0
                ? { line: tracked ? '#59c7ed' : '#e8a56f', point: tracked ? '#70e6bc' : '#f5cf70' }
                : { line: '#dca7ff', point: '#f5cf70' }
            context.strokeStyle = palette.line
            context.lineWidth = 2
            connections.forEach(([from, to]) => {
              const [x1, y1] = at(points[from])
              const [x2, y2] = at(points[to])
              context.beginPath()
              context.moveTo(x1, y1)
              context.lineTo(x2, y2)
              context.stroke()
            })
            context.fillStyle = palette.point
            points.forEach((point) => {
              const [x, y] = at(point)
              context.beginPath()
              context.arc(x, y, 3.2, 0, Math.PI * 2)
              context.fill()
            })
          })
      if (handDepthRelation === 'hand_farther') {
        drawHands()
        drawObject()
      } else {
        drawObject()
        drawHands()
      }
    }
    const observer = new ResizeObserver(draw)
    observer.observe(element)
    draw()
    return () => observer.disconnect()
  }, [
    cube,
    cubeSegments,
    handDepthRelation,
    hands,
    health.height,
    health.width,
    objectPoints,
    tracked,
  ])
  return <canvas className="hand-overlay" ref={canvas} aria-hidden="true" />
}

export function HandOverlay(props: Parameters<typeof DiagnosticOverlay>[0]) {
  const mode = useDemoMode()
  return mode === 'models' ? (
    <DiagnosticOverlay {...props} />
  ) : (
    <DemoEffects cameraId={props.cameraId} hands={props.hands} health={props.health} />
  )
}
