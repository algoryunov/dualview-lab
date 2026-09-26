# Procedural demo models

Original geometry generated for DualView CV Lab; no external models or textures.

- `car.glb`: turquoise toy car, 5,012 triangles, 482,424 bytes.
- `bed.glb`: wooden bed with blue duvet and cream pillows, 4,500 triangles, 445,284 bytes.
- `flower.glb`: pink flower in a terracotta pot, 6,456 triangles, 212,212 bytes.

All files embed their materials, require no external textures or compression decoders,
and use Y-up coordinates, a centered pivot, and a longest dimension of 1 unit.
The flower faces +Z; the car faces +X.

Regenerate from the repository root: `node frontend/generate-demo-models.mjs`.
The generator verifies each GLB by loading it back and checking its normalized bounds.

`SceneView` loads these files with `GLTFLoader` at runtime, selected through
`modelSelection.ts`. The fourth selectable model, `tower`, is built procedurally in
the renderer and has no GLB file.
