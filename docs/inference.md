# Inference providers

`dualview::inference::Model` exposes tensor contracts and execution. `Provider` reports availability/device information and loads models. `Registry` maps stable provider identifiers to factories. `Controller` selects one provider and reports a fallback reason when an explicitly enabled fallback is used.

The hand pipeline accepts a controller and loads its palm and landmark models through it. No Core ML types appear in hand processing or stereo geometry. The runtime controls cadence with `DUALVIEW_INFERENCE_MAX_FPS`, uses one processing worker, and retains the latest input instead of accumulating inference jobs.

## Existing providers

| ID | Runtime | Device policy |
| --- | --- | --- |
| `cpu` | ONNX Runtime CPUExecutionProvider | Portable CPU execution, one intra-op thread per session |
| `coreml` | ONNX Runtime CoreMLExecutionProvider | macOS, static input shapes, provider-controlled Apple acceleration |

Core ML may partition supported operators and execute remaining operators on CPU. `DUALVIEW_INFERENCE_ALLOW_CPU_FALLBACK` controls selection of a different provider when the requested provider is unavailable. It does not control ONNX Runtime's internal graph partitioning. Only an unavailable-provider report or a `ProviderUnavailable` exception permits that fallback. Invalid registrations and unexpected factory, model-loading, or execution errors remain explicit errors.

## Adding a provider

1. Implement `Provider` and `Model` in a new file under `src/inference/providers/`.
2. Report availability based on the loaded runtime/device, not just the OS name.
3. Register a factory in `default_registry()` and expose the implementation's build dependencies in CMake.
4. Preserve tensor name, shape, data type, and output ordering contracts. Keep image transforms in the hand pipeline.
5. Add provider-selection tests and real model-contract tests on the intended device.

A CUDA or remote implementation can use this boundary without changing the frontend. Those providers are extension points, not currently implemented backends.

The C++ model API is synchronous. It is called from the runtime's sole inference worker; callers must preserve that scheduling contract. Diagnostics include the selected provider, ONNX execution provider, fallback reason, processing time, and processing frequency.

## Hand image preparation

The landmark model uses the two-stage crop from the [OpenCV Zoo reference](https://github.com/opencv/opencv_zoo/blob/main/models/handpose_estimation_mediapipe/mp_handpose.py): expand and pad the detected palm before rotation, recompute bounds from the seven rotated palm landmarks, shift the bounds toward the fingers by 0.4 palm heights, enlarge by three, and square-pad before resizing to 224 pixels. The inverse transform includes both cropping stages and rotation. Border padding is black; crops are not stretched to fit a square.

`hand_crop_test` compares RGB input samples and all 21 inverse landmark coordinates against six golden cases generated with the upstream implementation: upright, two rotations, and three image-edge positions. The fixture records the reference source hash. This verifies preprocessing geometry and pixels, not detection accuracy on real hands. CPU/Core ML tensor tests remain separate. Live quality still requires a real two-camera run; confidence and stereo rejection thresholds have not been relaxed by this change.
