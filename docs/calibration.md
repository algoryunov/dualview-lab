# Camera calibration

Print the [A4 ChArUco target](targets/dualview-charuco-a4.pdf) at actual size. Verify a square measures 35 mm. A native generator is also available:

```bash
backend/build/dualview_charuco /tmp/dualview-target.png
```

The target uses 5 by 7 squares, 35 mm square size, 26 mm markers, and DICT_4X4_50. Do not resize the physical board without updating the shared specification.

1. Connect both cameras and open **Setup**.
2. Calibrate the laptop camera. Move the board through different positions, distances, and angles until 25 accepted views are collected.
3. Calibrate the phone in the exact camera mode/orientation you will use.
4. Fix both camera positions. Start **Calibrate pair** and show the board to both cameras for 20 accepted paired observations.
5. Inspect the reported reprojection error and baseline. Verify hand alignment in both camera previews.

Capture is count-based, not a fixed-duration recording. Each camera needs 25 distinct accepted target views; stereo needs 20 accepted frame pairs. Sampling is limited to once per 500 ms, so ideal captures take roughly 12 and 10 seconds respectively, plus solving time. In practice, board detection, diversity, and timing rejections take longer. Setup displays accepted/required counts, elapsed time, rejected counts, and the latest rejection or waiting reason. Capture continues until the target count is reached, an error occurs, or you cancel. The stereo target must be visible in both fresh frames with receive-time difference at most 80 ms; duplicate camera frames are not counted again.

Native calibration is saved to `data/calibration/native.yml` by default. Changing either camera or its crop, orientation, or physical position invalidates the corresponding calibration. Keep local files out of Git. Previously saved JSON profiles can be imported with the migration utility described in the verification guide; the originals are preserved.

On startup, the Setup diagnostics show the resolved `DUALVIEW_CALIBRATION_FILE` path and whether the native server loaded it. The path is relative to the directory where the server is launched, so `make dev` should be run from the repository root. Docker uses `/data/calibration/native.yml` in its persistent `calibration` volume; it intentionally does not read the host's `data/calibration` file unless you mount that directory explicitly.

The system checks input timing, valid intrinsics, positive depth, landmark confidence, and reprojection residuals. A receive-time difference above 80 ms blocks stereo reconstruction. This timing check cannot correct variable camera exposure/encode delays. Inspect real hardware behavior before relying on metric measurements.

## Cancellation and persistence ordering

Calibration computation runs on a private session copy. Cancel/restart and phone replacement invalidate that copy before it can publish geometry. A completed solution is saved before the success state is published. If saving has already begun, cancellation waits for that ordered save/commit; it does not interrupt filesystem operations. Frame input and preview delivery do not hold the persistence lock. A failed save is reported and does not publish the candidate solution.

Low reprojection error is necessary for consistency but does not prove metric accuracy. See the [reproducible evaluation](evaluation.md): an incorrect baseline or unobserved exposure delay can produce large 3D errors with nearly zero reprojection residual.
