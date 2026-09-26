import { handConnections as connections } from './handConnections'
import { followAmount } from './motion'
import { DemoEffects } from './DemoEffects'
import { useDemoMode } from './demoMode'
import type { StreamHealth } from './types'
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js'
import { demoModels, selectModel, useModelSelection } from './modelSelection'
import './scene.css'
import { useEffect, useRef, useState } from 'react'
import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'

type SceneData = {
  effectHands?: number[][][]
  effectHealth?: StreamHealth
  points: number[][] | null
  secondaryPoints: number[][] | null
  confidence: number[] | null
  cursor: number[] | null
  objectPosition: number[]
  objectScale: number
  objectRotation: number[]
  state: string
  objectStatus: string
}

type SceneObjects = {
  points: THREE.Mesh[]
  lines: THREE.Line[]
  secondaryPoints: THREE.Mesh[]
  secondaryLines: THREE.Line[]
  cursor: THREE.Mesh
  transformTarget: THREE.Object3D
  transformInitialized: boolean
  object: THREE.Group
  objectMaterial: THREE.PointsMaterial
  select: (index: number) => void
  controls: OrbitControls
  render: () => void
}

const toScene = (point: number[]) => new THREE.Vector3(point[0], -point[1], -point[2])

function towerPointGeometry() {
  const samples: THREE.Vector3[] = []
  const add = (start: THREE.Vector3, end: THREE.Vector3, count: number) => {
    for (let index = 0; index < count; index += 1)
      samples.push(start.clone().lerp(end, index / (count - 1)))
  }
  const base = 0.055
  const lower = [-1, 1].flatMap((x) =>
    [-1, 1].map((z) => new THREE.Vector3(x * base, -0.095, z * base)),
  )
  const upper = [-1, 1].flatMap((x) =>
    [-1, 1].map((z) => new THREE.Vector3(x * 0.014, 0.035, z * 0.014)),
  )
  lower.forEach((corner, index) => add(corner, upper[index], 18))
  ;[-0.055, -0.015, 0.025].forEach((y, ringIndex) => {
    const half = base * (1 - ringIndex * 0.32)
    const ring = [-1, 1].flatMap((x) =>
      [-1, 1].map((z) => new THREE.Vector3(x * half, y, z * half)),
    )
    ;[
      [0, 1],
      [1, 3],
      [3, 2],
      [2, 0],
    ].forEach(([from, to]) => add(ring[from], ring[to], 12))
  })
  lower.forEach((corner, index) => add(corner, upper[(index + 1) % 4], 15))
  const apex = new THREE.Vector3(0, 0.138, 0)
  upper.forEach((corner) => add(corner, apex, 20))
  return new THREE.BufferGeometry().setFromPoints(samples)
}

function ModelScene({
  points,
  secondaryPoints,
  confidence,
  cursor,
  objectPosition,
  objectScale,
  objectRotation,
  state,
  objectStatus,
}: SceneData) {
  const selected = useModelSelection()
  const [loadError, setLoadError] = useState('')
  const container = useRef<HTMLDivElement>(null)
  const objects = useRef<SceneObjects | null>(null)

  useEffect(() => {
    const host = container.current
    if (!host) return
    const scene = new THREE.Scene()
    scene.background = new THREE.Color('#080d17')
    const camera = new THREE.PerspectiveCamera(42, 1, 0.01, 10)
    camera.position.set(0, 0.12, 0.28)
    camera.lookAt(0, 0, -0.45)
    const renderer = new THREE.WebGLRenderer({
      antialias: true,
      powerPreference: 'high-performance',
    })
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
    host.appendChild(renderer.domElement)
    const controls = new OrbitControls(camera, renderer.domElement)
    controls.enableDamping = true
    controls.dampingFactor = 0.08
    controls.minDistance = 0.25
    controls.maxDistance = 2.6
    controls.target.set(0, 0, -0.45)
    scene.add(new THREE.AmbientLight('#9fc7ff', 1.5))
    const light = new THREE.DirectionalLight('#ffffff', 2.2)
    light.position.set(0.6, 0.8, 0.5)
    scene.add(light)
    const grid = new THREE.GridHelper(1.2, 12, '#324962', '#1b2c41')
    grid.rotation.x = Math.PI / 2
    grid.position.z = -0.7
    scene.add(grid)
    const fill = new THREE.DirectionalLight('#8ce5ff', 1.8)
    fill.position.set(-0.5, 0.3, 0.4)
    scene.add(fill)

    const pointGeometry = new THREE.SphereGeometry(0.008, 12, 10)
    const pointMaterial = new THREE.MeshStandardMaterial({ color: '#70e6bc', roughness: 0.35 })
    const handPoints = Array.from({ length: 21 }, () => {
      const mesh = new THREE.Mesh(pointGeometry, pointMaterial.clone())
      mesh.visible = false
      scene.add(mesh)
      return mesh
    })
    const handLines = connections.map(() => {
      const geometry = new THREE.BufferGeometry()
      geometry.setAttribute('position', new THREE.Float32BufferAttribute(6, 3))
      const line = new THREE.Line(geometry, new THREE.LineBasicMaterial({ color: '#59c7ed' }))
      line.visible = false
      scene.add(line)
      return line
    })
    const secondaryPointMaterial = new THREE.MeshStandardMaterial({
      color: '#f5cf70',
      roughness: 0.35,
    })
    const secondaryHandPoints = Array.from({ length: 21 }, () => {
      const mesh = new THREE.Mesh(pointGeometry, secondaryPointMaterial.clone())
      mesh.visible = false
      scene.add(mesh)
      return mesh
    })
    const secondaryHandLines = connections.map(() => {
      const geometry = new THREE.BufferGeometry()
      geometry.setAttribute('position', new THREE.Float32BufferAttribute(6, 3))
      const line = new THREE.Line(geometry, new THREE.LineBasicMaterial({ color: '#dca7ff' }))
      line.visible = false
      scene.add(line)
      return line
    })
    const cursorMesh = new THREE.Mesh(
      new THREE.SphereGeometry(0.016, 16, 12),
      new THREE.MeshStandardMaterial({
        color: '#f5cf70',
        emissive: '#5c4300',
        emissiveIntensity: 0.5,
      }),
    )
    cursorMesh.visible = false
    scene.add(cursorMesh)
    const towerMaterial = new THREE.PointsMaterial({
      color: '#d89f45',
      size: 0.008,
      sizeAttenuation: true,
    })
    const tower = new THREE.Points(towerPointGeometry(), towerMaterial)
    const objectRoot = new THREE.Group()
    const transformTarget = new THREE.Object3D()
    scene.add(objectRoot)
    const slots = demoModels.map(() => {
      const slot = new THREE.Group()
      slot.visible = false
      objectRoot.add(slot)
      return slot
    })
    slots[3].add(tower)
    const loaded = [false, false, false, true]
    let disposed = false
    let requested = 0
    let active = -1
    let transition: { from: number; to: number; start: number } | null = null
    const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)')
    const disposeObject = (root: THREE.Object3D) => {
      root.traverse((child) => {
        const mesh = child as THREE.Mesh
        mesh.geometry?.dispose()
        if (mesh.material)
          (Array.isArray(mesh.material) ? mesh.material : [mesh.material]).forEach((material) =>
            material.dispose(),
          )
      })
    }
    const switchTo = (index: number) => {
      requested = index
      if (!loaded[index] || active === index || transition) return
      setLoadError('')
      if (active < 0 || reducedMotion.matches) {
        slots.forEach((slot, i) => {
          slot.visible = i === index
          slot.scale.setScalar(1)
          slot.rotation.y = 0
        })
        active = index
        return
      }
      transition = { from: active, to: index, start: performance.now() }
      slots[index].visible = true
      slots[index].scale.setScalar(0.001)
    }
    const loader = new GLTFLoader()
    demoModels.slice(0, 3).forEach((model, index) => {
      loader.load(
        `/models/${model.id}.glb`,
        (gltf) => {
          if (disposed) {
            disposeObject(gltf.scene)
            return
          }
          gltf.scene.scale.multiplyScalar(0.24)
          // Give each model a readable three-quarter presentation within the gesture transform.
          gltf.scene.rotation.set(0.12, model.id === 'flower' ? -0.18 : -0.55, 0)
          slots[index].add(gltf.scene)
          loaded[index] = true
          if (requested === index) switchTo(index)
        },
        undefined,
        () => {
          if (!disposed)
            setLoadError(`Could not load ${model.label}. Refresh the page or choose another model.`)
        },
      )
    })
    let previousFrame = 0
    let frame = 0
    const animate = (now: number) => {
      if (disposed) return
      const dt = previousFrame ? Math.min((now - previousFrame) / 1000, 0.1) : 0
      previousFrame = now
      const amount = followAmount(dt)
      objectRoot.position.lerp(transformTarget.position, amount)
      objectRoot.scale.lerp(transformTarget.scale, amount)
      objectRoot.quaternion.slerp(transformTarget.quaternion, amount)
      if (transition) {
        const t = Math.min(1, (now - transition.start) / 850)
        const outgoing = slots[transition.from],
          incoming = slots[transition.to]
        const exit = Math.min(1, t / 0.55),
          enter = Math.max(0, (t - 0.25) / 0.75)
        const ease = (x: number) => x * x * (3 - 2 * x)
        outgoing.scale.setScalar(Math.max(0.001, 1 - ease(exit)))
        outgoing.rotation.y = ease(exit) * 1.3
        outgoing.position.y = -0.035 * ease(exit)
        incoming.scale.setScalar(Math.max(0.001, ease(enter)))
        incoming.rotation.y = -1.3 * (1 - ease(enter))
        incoming.position.y = 0.035 * (1 - ease(enter))
        if (t === 1) {
          outgoing.visible = false
          outgoing.position.y = 0
          active = transition.to
          transition = null
          switchTo(requested)
        }
      }
      controls.update()
      renderer.render(scene, camera)
      frame = requestAnimationFrame(animate)
    }
    frame = requestAnimationFrame(animate)
    objects.current = {
      points: handPoints,
      lines: handLines,
      secondaryPoints: secondaryHandPoints,
      secondaryLines: secondaryHandLines,
      cursor: cursorMesh,
      object: objectRoot,
      transformTarget,
      transformInitialized: false,
      select: switchTo,
      objectMaterial: towerMaterial,
      controls,
      render: () => renderer.render(scene, camera),
    }
    controls.addEventListener('change', () => renderer.render(scene, camera))
    const resetView = () => {
      camera.position.set(0, 0.12, 0.28)
      controls.target.set(0, 0, -0.45)
      controls.update()
      renderer.render(scene, camera)
    }
    renderer.domElement.addEventListener('dblclick', resetView)

    const resize = () => {
      const { width, height } = host.getBoundingClientRect()
      renderer.setSize(Math.max(width, 1), Math.max(height, 1), false)
      camera.aspect = Math.max(width, 1) / Math.max(height, 1)
      camera.updateProjectionMatrix()
      renderer.render(scene, camera)
    }
    const observer = new ResizeObserver(resize)
    observer.observe(host)
    resize()
    return () => {
      disposed = true
      cancelAnimationFrame(frame)
      disposeObject(scene)
      pointMaterial.dispose()
      secondaryPointMaterial.dispose()
      observer.disconnect()
      controls.dispose()
      renderer.domElement.removeEventListener('dblclick', resetView)
      renderer.dispose()
      host.removeChild(renderer.domElement)
      objects.current = null
    }
  }, [])

  useEffect(() => {
    objects.current?.select(selected)
  }, [selected])

  useEffect(() => {
    const sceneObjects = objects.current
    if (!sceneObjects) return
    const updateHand = (
      handPoints: number[][] | null,
      meshes: THREE.Mesh[],
      lines: THREE.Line[],
      useConfidence: boolean,
    ) => {
      const valid = handPoints?.length === 21 ? handPoints : null
      meshes.forEach((mesh, index) => {
        mesh.visible = Boolean(valid)
        if (!valid) return
        mesh.position.copy(toScene(valid[index]))
        if (useConfidence) {
          const score = confidence?.[index] ?? 0
          ;(mesh.material as THREE.MeshStandardMaterial).color.set(
            score >= 0.6 ? '#70e6bc' : '#e8a56f',
          )
        }
      })
      lines.forEach((line, index) => {
        line.visible = Boolean(valid)
        if (!valid) return
        const [from, to] = connections[index]
        const attribute = line.geometry.getAttribute('position') as THREE.BufferAttribute
        attribute.setXYZ(0, valid[from][0], -valid[from][1], -valid[from][2])
        attribute.setXYZ(1, valid[to][0], -valid[to][1], -valid[to][2])
        attribute.needsUpdate = true
      })
    }
    updateHand(points, sceneObjects.points, sceneObjects.lines, true)
    updateHand(secondaryPoints, sceneObjects.secondaryPoints, sceneObjects.secondaryLines, false)
    sceneObjects.cursor.visible = Boolean(cursor)
    if (cursor) sceneObjects.cursor.position.copy(toScene(cursor))
    sceneObjects.transformTarget.position.copy(toScene(objectPosition))
    sceneObjects.transformTarget.scale.setScalar(objectScale / 0.11)
    if (objectRotation.length === 4) {
      // toScene maps camera coordinates with a 180° X-axis basis change.
      sceneObjects.transformTarget.quaternion.set(
        objectRotation[0],
        -objectRotation[1],
        -objectRotation[2],
        objectRotation[3],
      )
    }
    if (!sceneObjects.transformInitialized) {
      sceneObjects.object.position.copy(sceneObjects.transformTarget.position)
      sceneObjects.object.scale.copy(sceneObjects.transformTarget.scale)
      sceneObjects.object.quaternion.copy(sceneObjects.transformTarget.quaternion)
      sceneObjects.transformInitialized = true
    }
    const color =
      objectStatus === 'committed' ? '#70e6bc' : state === 'grabbing' ? '#f5cf70' : '#d89f45'
    sceneObjects.objectMaterial.color.set(color)
    sceneObjects.render()
  }, [
    confidence,
    cursor,
    objectPosition,
    objectRotation,
    objectScale,
    objectStatus,
    points,
    secondaryPoints,
    state,
  ])

  return (
    <div
      className="scene-canvas demo-scene"
      ref={container}
      aria-label="Gesture-controlled 3D models"
    >
      <div className="model-picker" role="group" aria-label="Model selection">
        {demoModels.map((model, index) => (
          <button
            key={model.id}
            aria-pressed={selected === index}
            onClick={() => selectModel(index)}
            title={model.label}
          >
            <span aria-hidden="true">{model.icon}</span>
            <span>{model.label}</span>
          </button>
        ))}
      </div>
      <div className="model-caption">
        <strong aria-live="polite">{demoModels[selected].label}</strong>
        <span>☝ Hold for 0.9 s → next model</span>
        <small>Drag to orbit · scroll to zoom · double-click to reset</small>
      </div>
      {loadError && (
        <div className="model-error" role="alert">
          {loadError}
        </div>
      )}
    </div>
  )
}

const noCamera: StreamHealth = { available: false, width: null, height: null, age_ms: null }
export function SceneView(props: SceneData) {
  const mode = useDemoMode()
  return mode === 'models' ? (
    <ModelScene {...props} />
  ) : (
    <DemoEffects hands={props.effectHands ?? []} health={props.effectHealth ?? noCamera} preview />
  )
}
