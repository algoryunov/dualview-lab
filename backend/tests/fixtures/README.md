# Hand crop reference cases

`hand_crop_reference.json` contains numerical outputs from the OpenCV Zoo
`MPHandPose._preprocess` and `_postprocess` methods. The source URL, SHA-256,
and OpenCV version are recorded in the fixture. No photographs or model weights
are included.

The source image is 320 by 240 BGR pixels with channels `x % 251`, `y % 241`,
and `(x + y) % 239`. Each case supplies its palm box and seven landmarks.
`rgb_probes` sample the normalized 224 by 224 model input; `image_points` are
reference inverse projections of the supplied 21 `model_points`. Cases cover
upright and rotated palms, and clipping at the left, top, and bottom-right edges.
The reference is evaluated separately from the C++ code under test.
