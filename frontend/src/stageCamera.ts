import * as T from 'three'

// Match the ray-march shader: camera-space Y points down and its wide layout
// places the optical center at 56% of the viewport width.
export function alignStageCamera(
  camera: T.PerspectiveCamera,
  eye: T.Vector3,
  target: T.Vector3,
  width: number,
  height: number,
) {
  camera.position.copy(eye)
  camera.up.set(0, -1, 0)
  camera.aspect = width / height
  camera.lookAt(target)
  camera.updateMatrixWorld()
  camera.updateProjectionMatrix()
  camera.projectionMatrix.elements[8] = width / height > 1.5 ? -0.12 : 0
  camera.projectionMatrixInverse.copy(camera.projectionMatrix).invert()
}
