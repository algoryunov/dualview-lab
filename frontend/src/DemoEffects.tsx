import { LiquidMetal } from './LiquidMetal'
import { useEffect, useRef, useState } from 'react'
import * as T from 'three'
import { effectModes, fitVideo, useDemoMode, validHands } from './demoMode'
import { effectFragment } from './effectShader'
import type { StreamHealth } from './types'
import './effects.css'

type Props = {
  hands: number[][][]
  health: StreamHealth
  preview?: boolean
  cameraId?: 'laptop' | 'phone'
}
type TrailPoint = { x: number; y: number; born: number }
function FlatEffects({ hands, health, preview = false }: Props) {
  const mode = useDemoMode()
  const host = useRef<HTMLDivElement>(null)
  const data = useRef({ hands, health, mode, arrived: 0 })
  const [error, setError] = useState(false)
  useEffect(() => {
    data.current = { hands, health, mode, arrived: performance.now() }
  }, [hands, health, mode])
  useEffect(() => {
    const element = host.current
    if (!element) return
    let renderer: T.WebGLRenderer
    try {
      renderer = new T.WebGLRenderer({
        alpha: true,
        antialias: false,
        powerPreference: 'low-power',
      })
    } catch {
      setError(true)
      return
    }
    renderer.setClearColor(0, 0)
    const canvas = document.createElement('canvas')
    const ctx = canvas.getContext('2d')!
    element.append(renderer.domElement, canvas)
    const scene = new T.Scene(),
      camera = new T.Camera()
    const uniforms = {
      resolution: { value: new T.Vector2(1, 1) },
      center: { value: new T.Vector2() },
      radius: { value: 100 },
      time: { value: 0 },
      angle: { value: 0 },
      spread: { value: 0.5 },
      mode: { value: 0 },
      opacity: { value: 0 },
    }
    const material = new T.ShaderMaterial({
      uniforms,
      vertexShader: 'void main(){gl_Position=vec4(position.xy,0.,1.);}',
      fragmentShader: effectFragment,
      transparent: true,
      depthTest: false,
      depthWrite: false,
    })
    const geometry = new T.PlaneGeometry(2, 2)
    scene.add(new T.Mesh(geometry, material))
    let width = 1,
      height = 1,
      visible = true,
      frame = 0,
      last = 0,
      start = performance.now()
    let shownMode = data.current.mode,
      fade = 0
    const trails: TrailPoint[][] = Array.from({ length: 10 }, () => [])
    const tips = Array.from({ length: 10 }, () => new T.Vector2())
    let hadHands = false
    const resize = () => {
      width = Math.max(1, element.clientWidth)
      height = Math.max(1, element.clientHeight)
      // Bound fragment cost, especially for ray-marched metal and multiple previews.
      const ratio = Math.min(1.25, 1000 / Math.max(width, height))
      renderer.setPixelRatio(ratio)
      renderer.setSize(width, height)
      canvas.width = Math.round(width * ratio)
      canvas.height = Math.round(height * ratio)
      ctx.setTransform(ratio, 0, 0, ratio, 0, 0)
      uniforms.resolution.value.set(width * ratio, height * ratio)
      trails.forEach((t) => t.splice(0))
      hadHands = false
    }
    const observer = new ResizeObserver(resize)
    observer.observe(element)
    resize()
    const visibility = new IntersectionObserver((entries) => {
      visible = entries[0].isIntersecting
    })
    visibility.observe(element)
    const reduced = window.matchMedia('(prefers-reduced-motion: reduce)')
    const draw = (now: number) => {
      frame = requestAnimationFrame(draw)
      if (!visible || document.hidden) {
        last = now
        return
      }
      const dt = Math.min(0.05, (now - (last || now)) / 1000)
      last = now
      const { hands: incoming, health: stream, mode: requested, arrived } = data.current
      const live = stream.available && (stream.age_ms ?? Infinity) < 800 && now - arrived < 800
      const actual = live ? validHands(incoming) : []
      const synthetic = preview && !actual.length
      const elapsed = reduced.matches ? 1 : (now - start) / 1000
      const video = fitVideo(width, height, stream.width || width, stream.height || height)
      const point = (p: number[]) =>
        new T.Vector2(video.left + p[0] * video.width, video.top + p[1] * video.height)
      const palms = actual.map((h) =>
        point([(h[0][0] + h[5][0] + h[17][0]) / 3, (h[0][1] + h[5][1] + h[17][1]) / 3]),
      )
      if (synthetic)
        palms.push(
          new T.Vector2(width * (0.32 + 0.035 * Math.sin(elapsed)), height * 0.52),
          new T.Vector2(
            width * (0.68 - 0.035 * Math.sin(elapsed)),
            height * (0.48 + 0.08 * Math.sin(elapsed * 0.5)),
          ),
        )
      const present = palms.length > 0
      const amount = 1 - Math.exp(-dt * 9)
      const center =
        palms.length === 2
          ? palms[0].clone().add(palms[1]).multiplyScalar(0.5)
          : palms[0] || new T.Vector2(width / 2, height / 2)
      const distance =
        palms.length === 2 ? palms[0].distanceTo(palms[1]) : Math.min(width, height) * 0.32
      const radius = T.MathUtils.clamp(
        distance * 0.42,
        Math.min(width, height) * 0.065,
        Math.min(width, height) * 0.34,
      )
      const targetAngle =
        palms.length === 2 ? Math.atan2(palms[1].y - palms[0].y, palms[1].x - palms[0].x) : 0
      const ratio = renderer.getPixelRatio()
      uniforms.center.value.lerp(
        new T.Vector2(center.x * ratio, (height - center.y) * ratio),
        hadHands ? amount : 1,
      )
      uniforms.radius.value = T.MathUtils.lerp(uniforms.radius.value, radius * ratio, amount)
      const delta = Math.atan2(
        Math.sin(targetAngle - uniforms.angle.value),
        Math.cos(targetAngle - uniforms.angle.value),
      )
      uniforms.angle.value += delta * amount
      uniforms.spread.value = T.MathUtils.lerp(
        uniforms.spread.value,
        (distance / Math.min(width, height)) * 2,
        amount,
      )
      if (requested !== shownMode) {
        fade = Math.max(0, fade - dt * 5)
        if (fade === 0) {
          shownMode = requested
          trails.forEach((t) => t.splice(0))
          start = now
        }
      } else fade = Math.min(1, fade + dt * 3)
      uniforms.mode.value = shownMode === 'energy' ? 0 : 1
      uniforms.time.value = elapsed
      uniforms.opacity.value = T.MathUtils.lerp(uniforms.opacity.value, present ? fade : 0, amount)
      renderer.render(scene, camera)
      ctx.clearRect(0, 0, width, height)
      if (shownMode === 'trails') {
        const targets = actual.flatMap((h) => [4, 8, 12, 16, 20].map((i) => point(h[i])))
        if (synthetic)
          for (let i = 0; i < 10; i++)
            targets.push(
              new T.Vector2(
                width * 0.5 + Math.sin(elapsed * 0.9 + i * 0.16) * width * 0.28,
                height * 0.5 + Math.sin(elapsed * 1.7 + i * 0.17) * height * 0.23,
              ),
            )
        targets.forEach((p, i) => {
          if (!hadHands || tips[i].distanceTo(p) > Math.min(width, height) * 0.4) {
            tips[i].copy(p)
            trails[i] = []
          }
          tips[i].lerp(p, amount)
          trails[i].push({ x: tips[i].x, y: tips[i].y, born: now })
          if (trails[i].length > 100) trails[i].shift()
        })
        ctx.globalCompositeOperation = 'lighter'
        trails.forEach((trail, i) => {
          while (trail.length && now - trail[0].born > 1400) trail.shift()
          const hue = 175 + i * 13
          for (let j = 1; j < trail.length; j++) {
            const a = trail[j - 1],
              b = trail[j],
              life = Math.max(0, 1 - (now - b.born) / 1400) * fade
            ctx.strokeStyle = `hsla(${hue},95%,65%,${life * 0.6})`
            ctx.shadowColor = `hsl(${hue},100%,60%)`
            ctx.shadowBlur = 12
            ctx.lineWidth = 1 + life * 3
            ctx.lineCap = 'round'
            ctx.beginPath()
            ctx.moveTo(a.x, a.y)
            ctx.lineTo(b.x, b.y)
            ctx.stroke()
          }
          if (targets[i]) {
            ctx.fillStyle = '#e3ffff'
            ctx.beginPath()
            ctx.arc(tips[i].x, tips[i].y, 3, 0, Math.PI * 2)
            ctx.fill()
          }
        })
        ctx.shadowBlur = 0
        ctx.globalCompositeOperation = 'source-over'
      }
      hadHands = present
    }
    frame = requestAnimationFrame(draw)
    return () => {
      cancelAnimationFrame(frame)
      observer.disconnect()
      visibility.disconnect()
      geometry.dispose()
      material.dispose()
      renderer.dispose()
      renderer.domElement.remove()
      canvas.remove()
    }
  }, [preview])
  const current = effectModes.find((m) => m.id === mode)!
  return (
    <div
      ref={host}
      className={`demo-effect ${preview ? 'effect-preview' : ''}`}
      aria-label={current.label}
    >
      {preview && (
        <div className="effect-caption">
          <strong>{current.label}</strong>
          <span>{current.hint}</span>
          <small>
            {validHands(hands).length && health.available && (health.age_ms ?? Infinity) < 800
              ? 'Hand control'
              : 'Auto demo · show your hands to the camera'}
          </small>
        </div>
      )}
      {error && (
        <span className="effect-error" role="alert">
          Effects require WebGL. Try another browser.
        </span>
      )}
    </div>
  )
}

export function DemoEffects(props: Props) {
  const mode = useDemoMode()
  return mode === 'metal' ? (
    <LiquidMetal preview={props.preview} cameraId={props.cameraId} />
  ) : (
    <FlatEffects {...props} />
  )
}
