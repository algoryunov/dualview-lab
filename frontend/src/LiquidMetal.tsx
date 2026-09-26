import { MetalHandTracker } from './metalTracking'
import { handConnections } from './handConnections'
import { alignStageCamera } from './stageCamera'
import {
  followAmount,
  liquidHalfSpan,
  liquidDrift,
  liquidCompression,
  liquidSqueezeLift,
  springStep,
} from './motion'
import { fluidRadii, fluidNodes } from './fluidShape'
import { hasFreshFrame } from './streamHealth'
import { metalBlockReason } from './metalReadiness'
import { useEffect, useRef, useState } from 'react'
import * as T from 'three'
import { useSpatialTracking } from './spatialContext'
import { palmCenter, projectionMatches, toCamera } from './spatialMath'
import type { Vec3 } from './spatialMath'
import { fitVideo } from './demoMode'
import { changeMetalSettings, useMetalSettings } from './metalSettings'
import { metalFragment, metalVertex } from './metalShader'
import './liquid-metal.css'

type Props = { preview?: boolean; cameraId?: 'laptop' | 'phone' }
const UNIT = 0.075
const MAX_HAND_POINTS = 42
// Matches the virtual ray-march camera's vertical field of view (tan(fov / 2) = .46).
const STAGE_FOV = (2 * Math.atan(0.46) * 180) / Math.PI
export function LiquidMetal({ preview = false, cameraId = 'laptop' }: Props) {
  const tracking = useSpatialTracking(),
    settings = useMetalSettings()
  const input = useRef({ tracking, settings })
  const host = useRef<HTMLDivElement>(null)
  const [status, setStatus] = useState({
    live: false,
    hands: 0,
    fps: 0,
    message: 'Show one palm to both cameras.',
    counts: 'Laptop: — · Phone: —',
  })
  const [error, setError] = useState('')
  useEffect(() => {
    input.current = { tracking, settings }
  }, [tracking, settings])
  useEffect(() => {
    const element = host.current
    if (!element) return
    let renderer: T.WebGLRenderer
    try {
      renderer = new T.WebGLRenderer({
        alpha: !preview,
        antialias: false,
        powerPreference: 'high-performance',
      })
    } catch {
      setError('WebGL is unavailable. Use a browser with hardware acceleration.')
      return
    }
    renderer.setClearColor('#060b13', preview ? 1 : 0)
    element.prepend(renderer.domElement)
    const scene = new T.Scene(),
      camera = new T.Camera()
    // The metal is ray-marched in a virtual scene. Draw the landmark cloud in a
    // normal Three.js pass immediately afterwards so it shares that scene/camera.
    const handScene = new T.Scene()
    const handCamera = new T.PerspectiveCamera(STAGE_FOV, 1, 0.01, 20)
    const handPositions = new Float32Array(MAX_HAND_POINTS * 3)
    const handColors = new Float32Array(MAX_HAND_POINTS * 3)
    const handGeometry = new T.BufferGeometry()
    const handPositionAttribute = new T.BufferAttribute(handPositions, 3)
    const handColorAttribute = new T.BufferAttribute(handColors, 3)
    handGeometry.setAttribute('position', handPositionAttribute)
    handGeometry.setAttribute('color', handColorAttribute)
    handGeometry.setDrawRange(0, 0)
    const handGlowMaterial = new T.PointsMaterial({
      size: 0.16,
      sizeAttenuation: true,
      vertexColors: true,
      transparent: true,
      opacity: 0.16,
      depthTest: false,
      depthWrite: false,
      blending: T.AdditiveBlending,
    })
    const handPointMaterial = new T.PointsMaterial({
      size: 0.066,
      sizeAttenuation: true,
      vertexColors: true,
      transparent: true,
      opacity: 0.96,
      depthTest: false,
      depthWrite: false,
    })
    const handGlow = new T.Points(handGeometry, handGlowMaterial)
    const handPoints = new T.Points(handGeometry, handPointMaterial)
    handGlow.frustumCulled = false
    handPoints.frustumCulled = false
    const boneGeometry = new T.BufferGeometry()
    boneGeometry.setAttribute('position', handPositionAttribute)
    boneGeometry.setAttribute('color', handColorAttribute)
    boneGeometry.setIndex(
      [0, 21].flatMap((offset) => handConnections.flatMap(([a, b]) => [a + offset, b + offset])),
    )
    boneGeometry.setDrawRange(0, 0)
    const boneMaterial = new T.LineBasicMaterial({
      vertexColors: true,
      transparent: true,
      opacity: 0.7,
      depthTest: false,
      depthWrite: false,
    })
    const handBones = new T.LineSegments(boneGeometry, boneMaterial)
    handBones.frustumCulled = false
    handScene.add(handBones, handGlow, handPoints)
    const blobs = Array.from({ length: 5 }, () => new T.Vector4(0, 0, 0, 0.5))
    const velocities = Array.from({ length: 5 }, () => new T.Vector3())
    const uniforms = {
      resolution: { value: new T.Vector2(1, 1) },
      viewport: { value: new T.Vector4(0, 0, 1, 1) },
      intrinsics: { value: new T.Vector4(1, 1, 0.5, 0.5) },
      lens: { value: Array(8).fill(0) },
      origin: { value: new T.Vector3(0, 0, 0.6) },
      eye: { value: new T.Vector3(0, -0.45, -5) },
      target: { value: new T.Vector3(0, -0.05, 0) },
      cameraToWorld: { value: new T.Matrix3() },
      blobs: { value: blobs },
      flow: { value: Array.from({ length: 9 }, () => new T.Vector4(0, 0, 0, 0.5)) },
      fingers: { value: Array.from({ length: 42 }, () => new T.Vector4()) },
      handCount: { value: 0 },
      stretched: { value: 0 },
      time: { value: 0 },
      opacity: { value: 0 },
      agitation: { value: 0 },
      fusion: { value: 0.48 },
      finish: { value: 0 },
      stage: { value: preview },
      occlusion: { value: true },
      quality: { value: 95 },
    }
    const geometry = new T.PlaneGeometry(2, 2)
    const material = new T.ShaderMaterial({
      uniforms,
      vertexShader: metalVertex,
      fragmentShader: metalFragment,
      transparent: !preview,
      depthTest: false,
      depthWrite: false,
    })
    scene.add(new T.Mesh(geometry, material))
    renderer.debug.onShaderError = () => {
      setError('Could not initialize the material. Try updating your browser.')
    }
    let width = 1,
      height = 1,
      ratio = 1,
      intersecting = true,
      frame = 0,
      previous = 0,
      clock = 0,
      lastUI = 0,
      frameCount = 0,
      frameTime = 0
    let yaw = -0.25,
      pitch = -0.12,
      zoom = 4.7,
      pointer: { x: number; y: number; id: number } | null = null
    let lastDistance = 0.15,
      burst = 0
    const reduced = window.matchMedia('(prefers-reduced-motion: reduce)')
    const center = new T.Vector3(0, 0, 0.6)
    const previousAxis = new T.Vector3(1, 0, 0)
    const handTracker = new MetalHandTracker()
    const lastCenter = center.clone(),
      materialVelocity = new T.Vector3()
    let initialized = false,
      lastMetalLog = 0
    let anchor: T.Vector3 | null = null
    const onDown = (e: PointerEvent) => {
      if (!preview || (e.target as HTMLElement).closest('button')) return
      pointer = { x: e.clientX, y: e.clientY, id: e.pointerId }
      element.setPointerCapture(e.pointerId)
    }
    const onMove = (e: PointerEvent) => {
      if (!pointer) return
      yaw += (e.clientX - pointer.x) * 0.006
      pitch = T.MathUtils.clamp(pitch + (e.clientY - pointer.y) * 0.004, -0.7, 0.7)
      pointer.x = e.clientX
      pointer.y = e.clientY
    }
    const onUp = () => {
      pointer = null
    }
    const onWheel = (e: WheelEvent) => {
      if (!preview) return
      e.preventDefault()
      zoom = T.MathUtils.clamp(zoom + e.deltaY * 0.004, 3.7, 8)
    }
    const onReset = () => {
      yaw = -0.25
      pitch = -0.12
      zoom = 4.7
    }
    if (preview) {
      element.addEventListener('pointerdown', onDown)
      element.addEventListener('pointermove', onMove)
      element.addEventListener('pointerup', onUp)
      element.addEventListener('pointercancel', onUp)
      element.addEventListener('wheel', onWheel, { passive: false })
      element.addEventListener('dblclick', onReset)
    }
    const resize = () => {
      width = Math.max(1, element.clientWidth)
      height = Math.max(1, element.clientHeight)
      ratio = Math.min(
        window.devicePixelRatio,
        1.5,
        (preview ? 1400 : 1100) / Math.max(width, height),
      )
      renderer.setPixelRatio(ratio)
      renderer.setSize(width, height, false)
      uniforms.resolution.value.set(Math.floor(width * ratio), Math.floor(height * ratio))
      handCamera.aspect = width / height
      handCamera.updateProjectionMatrix()
    }
    const observer = new ResizeObserver(resize)
    observer.observe(element)
    resize()
    const visibility = new IntersectionObserver((entries) => {
      intersecting = entries[0].isIntersecting
    })
    visibility.observe(element)
    const animate = (now: number) => {
      frame = requestAnimationFrame(animate)
      if (!intersecting || document.hidden) {
        previous = now
        return
      }
      const elapsedFrame = (now - (previous || now)) / 1000
      const dt = Math.min(0.035, elapsedFrame)
      previous = now
      clock = reduced.matches ? 0 : now / 1000
      const {
        tracking: { telemetry, cameras, received, cameraIssue },
        settings: opts,
      } = input.current
      const stream = telemetry?.[cameraId],
        hand = telemetry?.hand_tracking,
        calibration = cameras?.[cameraId]
      const fresh = !!stream && hasFreshFrame(stream) && now - received < 650
      const stereoFresh =
        !!telemetry && [telemetry.laptop, telemetry.phone].every((s) => hasFreshFrame(s))
      const usable = fresh && stereoFresh && telemetry?.calibration.status === 'calibrated'
      const hands = handTracker.update(telemetry, received, now, usable)
      const aligned = projectionMatches(calibration, stream?.width ?? null, stream?.height ?? null)
      const live = opts.input === 'hands' && hands.length > 0 && (preview || aligned)
      const auto = preview && opts.input === 'auto'
      const palms = hands.map(palmCenter)
      const a = new T.Vector3(),
        b = new T.Vector3()
      let distance = 0.15
      if (live) {
        a.fromArray(palms[0])
        b.fromArray(palms[1] ?? palms[0])
        const mid = a.clone().add(b).multiplyScalar(0.5)
        // Smooth the hand anchor; material drifts independently around it.
        center.lerp(mid, initialized ? followAmount(dt, 10) : 1)
        distance =
          palms.length === 2
            ? new T.Vector3(...palms[0]).distanceTo(new T.Vector3(...palms[1]))
            : 0.12
        if (palms.length === 1) {
          a.fromArray(hands[0][5])
          b.fromArray(hands[0][17])
        }
      } else if (auto) {
        anchor = null
        a.set(-1, -0.12, -0.18)
        b.set(1, 0.12, 0.18)
        distance = 0.18 + 0.12 * Math.sin(clock * 0.42) + 0.025 * Math.sin(clock * 0.93)
      }
      const axis = b.clone().sub(a).normalize()
      if (axis.lengthSq() < 0.5) axis.set(1, 0, 0)
      // Endpoints are symmetric; retain orientation when detector hand order changes.
      if (axis.dot(previousAxis) < 0) axis.negate()
      // Nearly coincident palms do not define a reliable stretch direction.
      if (live && palms.length === 2 && distance < 0.025) axis.copy(previousAxis)
      else axis.copy(previousAxis.clone().lerp(axis, followAmount(dt, 12)).normalize())
      previousAxis.copy(axis)
      const across = new T.Vector3().crossVectors(axis, new T.Vector3(0, 0, 1)).normalize()
      if (across.lengthSq() < 0.5) across.set(0, 1, 0)
      const separation =
        live && palms.length === 2
          ? liquidHalfSpan(distance, UNIT)
          : T.MathUtils.clamp((distance - 0.07) / 0.25, 0, 1.7)
      const compression = (live && palms.length === 2) || auto ? liquidCompression(distance) : 0
      const speed = Math.min(1, (Math.abs(distance - lastDistance) / Math.max(dt, 0.01)) * 0.4)
      lastDistance = distance
      burst = Math.max(speed, burst * Math.exp(-dt * 3))
      const motion = center.clone().sub(lastCenter).divideScalar(Math.max(dt, 0.001))
      lastCenter.copy(center)
      if (!initialized || !live) motion.set(0, 0, 0)
      materialVelocity.lerp(motion.clampLength(0, 1.5), followAmount(dt, 8))
      const drag = materialVelocity.clone().multiplyScalar(-0.045 / UNIT)
      drag.addScaledVector(axis, -drag.dot(axis))
      const gravity = new T.Vector3(0, 1, 0).addScaledVector(axis, -axis.y)
      const drift = new T.Vector3(...(reduced.matches ? [0, 0, 0] : liquidDrift(clock)))
      const fluidity = Math.min(1, separation / 0.4)
      const goals = Array.from({ length: 5 }, (_, i) => {
        const u = i / 4,
          arch = Math.sin(Math.PI * u)
        return axis
          .clone()
          .multiplyScalar((2 * u - 1) * separation)
          .add(drift)
          .add(new T.Vector3(0, liquidSqueezeLift(i, compression), 0))
          .addScaledVector(drag, arch * fluidity)
          .addScaledVector(gravity, arch * Math.min(0.42, separation * 0.16))
          .addScaledVector(
            across,
            arch *
              Math.sin(clock * 1.4 - u * 4) *
              fluidity *
              (reduced.matches ? 0 : 0.09 + burst * 0.18),
          )
      })
      for (let i = 0; i < 5; i++) {
        const goal = goals[i]
        if (!live && !auto) continue
        const position = new T.Vector3(blobs[i].x, blobs[i].y, blobs[i].z)
        if (!initialized) {
          position.copy(goal)
          velocities[i].set(0, 0, 0)
        }
        // Analytic spring avoids refresh-rate-dependent stiffness and overshoot.
        for (const axis of ['x', 'y', 'z'] as const) {
          ;[position[axis], velocities[i][axis]] = springStep(
            position[axis],
            velocities[i][axis],
            goal[axis],
            dt,
            i === 0 || i === 4 ? 10 : 6,
          )
        }
        blobs[i].set(position.x, position.y, position.z, blobs[i].w)
      }
      if (live || auto) {
        const lengths = blobs
          .slice(1)
          .map((p, i) => Math.hypot(p.x - blobs[i].x, p.y - blobs[i].y, p.z - blobs[i].z))
        const radii = fluidRadii(lengths, clock, burst, compression)
        blobs.forEach((blob, i) => {
          blob.w = radii[i]
        })
      }
      fluidNodes(blobs.map((blob) => blob.toArray())).forEach((node, i) =>
        uniforms.flow.value[i].fromArray(node),
      )
      uniforms.origin.value.copy(center)
      uniforms.agitation.value = burst
      uniforms.stretched.value = T.MathUtils.lerp(
        uniforms.stretched.value,
        live && palms.length === 2 ? 1 : 0,
        followAmount(dt, 12),
      )
      uniforms.fusion.value = 0.38 + 0.14 * (1 - Math.min(1, separation))
      if (live || auto) uniforms.time.value = clock
      uniforms.finish.value = opts.finish
      uniforms.occlusion.value = opts.occlusion
      uniforms.opacity.value = T.MathUtils.lerp(
        uniforms.opacity.value,
        live || auto ? 1 : preview ? 0.25 : 0,
        1 - Math.exp(-dt * 10),
      )
      if (live || auto) initialized = true
      if (preview && now - lastMetalLog >= 100) {
        lastMetalLog = now
        input.current.tracking.reportMetal?.({
          renderer: 'swept-fluid-v3',
          mode: opts.input,
          phase: auto ? 'auto' : live ? (palms.length === 2 ? 'two_hands' : 'one_hand') : 'blocked',
          camera: cameraId,
          client_ms: now,
          dt_ms: elapsedFrame * 1000,
          hands: hands.length,
          distance_m: distance,
          separation,
          compression,
          predicted: handTracker.predicted,
          neck: uniforms.stretched.value,
          opacity: uniforms.opacity.value,
          center_m: center.toArray(),
          axis: axis.toArray(),
          blobs: blobs.flatMap((blob) => blob.toArray()),
          velocities: velocities.flatMap((v) => v.toArray()),
        })
      }
      if (preview) {
        const orbit = yaw + (auto && !reduced.matches ? Math.sin(clock * 0.12) * 0.08 : 0)
        uniforms.eye.value.set(
          Math.sin(orbit) * zoom,
          Math.sin(pitch) * zoom,
          -Math.cos(orbit) * Math.cos(pitch) * zoom,
        )
        uniforms.target.value.set(0, -0.05, 0)
        if (live && !anchor) anchor = center.clone()
        if (anchor && opts.input === 'hands') {
          const offset = center.clone().sub(anchor).divideScalar(UNIT)
          uniforms.eye.value.sub(offset)
          uniforms.target.value.sub(offset)
        }
        uniforms.cameraToWorld.value.identity()
        // The shader moves its optical center right on the wide desktop stage to
        // leave room for the copy. Apply the matching projection offset to points.
        alignStageCamera(handCamera, uniforms.eye.value, uniforms.target.value, width, height)
        let pointCount = 0
        if (live)
          hands.forEach((points, handIndex) =>
            points.forEach((point, landmarkIndex) => {
              const index = pointCount++
              handPositions[index * 3] = (point[0] - center.x) / UNIT
              handPositions[index * 3 + 1] = (point[1] - center.y) / UNIT
              handPositions[index * 3 + 2] = (point[2] - center.z) / UNIT
              const confidence =
                handIndex === 0
                  ? (hand?.landmark_confidence?.[landmarkIndex] ?? 0.5)
                  : (hand?.secondary_confidence ?? 0.5)
              const brightness = T.MathUtils.clamp(0.42 + confidence * 0.58, 0.42, 1)
              handColors[index * 3] = handIndex === 0 ? 0.25 * brightness : 0.94 * brightness
              handColors[index * 3 + 1] = handIndex === 0 ? 0.9 * brightness : 0.4 * brightness
              handColors[index * 3 + 2] = handIndex === 0 ? 1 * brightness : 0.92 * brightness
            }),
          )
        boneGeometry.setDrawRange(0, (pointCount / 21) * handConnections.length * 2)
        handGeometry.setDrawRange(0, pointCount)
        handPositionAttribute.needsUpdate = true
        handColorAttribute.needsUpdate = true
      } else if (calibration && stream) {
        uniforms.origin.value.fromArray(toCamera(center.toArray() as Vec3, calibration))
        const fit = fitVideo(width, height, calibration.width, calibration.height)
        uniforms.viewport.value.set(
          fit.left * ratio,
          fit.top * ratio,
          fit.width * ratio,
          fit.height * ratio,
        )
        const [fx, fy, cx, cy] = calibration.intrinsics
        uniforms.intrinsics.value.set(
          fx / calibration.width,
          fy / calibration.height,
          cx / calibration.width,
          cy / calibration.height,
        )
        uniforms.lens.value = calibration.distortion
        const r = calibration.rotation
        uniforms.cameraToWorld.value
          .set(
            ...(r.flat() as [
              number,
              number,
              number,
              number,
              number,
              number,
              number,
              number,
              number,
            ]),
          )
          .transpose()
        uniforms.handCount.value = live ? hands.length : 0
        if (live)
          hands.forEach((points, h) =>
            points.forEach((point, i) => {
              const p = new T.Vector3(...point).sub(center).divideScalar(UNIT)
              uniforms.fingers.value[h * 21 + i].set(p.x, p.y, p.z, i % 4 === 0 ? 0.075 : 0.095)
            }),
          )
      }
      renderer.autoClear = false
      renderer.clear()
      renderer.render(scene, camera)
      if (preview) {
        renderer.clearDepth()
        renderer.render(handScene, handCamera)
      }
      renderer.autoClear = true
      frameCount++
      frameTime += elapsedFrame
      if (now - lastUI > 700) {
        const message = live
          ? hands.length === 2
            ? 'Hand control is active. Spread your palms to stretch the metal; bring them closer to merge, then gently squeeze to lift the center.'
            : 'Move one palm in any direction, including depth. Use two palms to pull the metal apart in 3D.'
          : opts.input === 'auto'
            ? 'Auto demo is active. Select Hand control to interact.'
            : !preview && !aligned
              ? cameraIssue || 'Calibrate both cameras to align the 3D overlay.'
              : metalBlockReason(telemetry, now - received)
        const counts = `Laptop: ${hand?.laptop_hands_landmarks_normalized?.length ?? 0} hands · Phone: ${hand?.phone_hands_landmarks_normalized?.length ?? 0} hands · 3D landmarks: ${hands.length * 21}`
        setStatus({
          live,
          hands: hands.length,
          fps: Math.round(frameCount / Math.max(frameTime, 0.01)),
          message,
          counts,
        })
        frameCount = 0
        frameTime = 0
        lastUI = now
      }
    }
    frame = requestAnimationFrame(animate)
    return () => {
      cancelAnimationFrame(frame)
      observer.disconnect()
      visibility.disconnect()
      geometry.dispose()
      material.dispose()
      boneGeometry.dispose()
      boneMaterial.dispose()
      handGeometry.dispose()
      handGlowMaterial.dispose()
      handPointMaterial.dispose()
      renderer.dispose()
      renderer.forceContextLoss()
      renderer.domElement.remove()
      element.removeEventListener('pointerdown', onDown)
      element.removeEventListener('pointermove', onMove)
      element.removeEventListener('pointerup', onUp)
      element.removeEventListener('pointercancel', onUp)
      element.removeEventListener('wheel', onWheel)
      element.removeEventListener('dblclick', onReset)
    }
  }, [preview, cameraId])
  return (
    <div
      ref={host}
      className={`liquid-metal ${preview ? 'metal-stage' : 'metal-overlay'}`}
      aria-label="Liquid Metal spatial scene"
    >
      {preview && (
        <>
          <div className="metal-heading">
            <span className="metal-kicker">DUALVIEW / MATERIAL STUDY 01</span>
            <h2>
              Matter
              <br />
              <em>in your hands.</em>
            </h2>
            <p>
              1. Show one palm to both cameras.
              <br />
              2. Wait for LIVE · 3D, then move your hand.
              <br />
              3. Add a second palm to stretch the metal.
              <br />
              No pinch or activation gesture required.
            </p>
          </div>
          <div className="metal-status" role="status">
            <i className={status.live ? 'is-live' : ''} />
            {status.live
              ? 'LIVE · 3D'
              : settings.input === 'auto'
                ? 'AUTO DEMO'
                : 'WAITING FOR HANDS'}
            <span>{status.fps} FPS</span>
          </div>
          <div className="metal-bottom">
            <div className="metal-input" role="group" aria-label="Input source">
              <button
                aria-pressed={settings.input === 'hands'}
                onClick={() => changeMetalSettings({ input: 'hands' })}
              >
                Hand control
              </button>
              <button
                aria-pressed={settings.input === 'auto'}
                onClick={() => changeMetalSettings({ input: 'auto' })}
              >
                Auto demo
              </button>
            </div>
            <div className="metal-finishes" role="group" aria-label="Material">
              {['Mercury', 'Iridium', 'Gold'].map((label, i) => (
                <button
                  key={label}
                  aria-pressed={settings.finish === i}
                  onClick={() => changeMetalSettings({ finish: i })}
                >
                  <i className={`finish-${i}`} />
                  {label}
                </button>
              ))}
            </div>
            <p className={status.live ? 'metal-ready' : 'metal-guidance'} role="status">
              {status.message}
            </p>
            <p className="metal-counts">{status.counts}</p>
            {tracking.metalLogStatus && (
              <p className="metal-counts">Metal log: {tracking.metalLogStatus}</p>
            )}
            <small>Drag to orbit · scroll to zoom · double-click to reset</small>
          </div>
        </>
      )}
      {!preview && !status.live && <span className="metal-camera-status">{status.message}</span>}
      {error && (
        <div role="alert" className="metal-error">
          {error}
        </div>
      )}
    </div>
  )
}
