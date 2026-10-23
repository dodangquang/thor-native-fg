Production spatial reconstruction quality lab

This lab calls the same Engine::recordSpatial and generated shaders used by
the layer. It does not run a substitute CPU model for GPU quality scores.
The only CPU reconstruction is an independent bilinear/control clamp definition.
All scene artwork/geometry/HUD glyphs are authored here. No QRISP images,
restricted training dataset, external game assets or external fonts are shipped.

prepare.py renders low-resolution input independently from its high-resolution
reference: one sample per low-res pixel versus four linear subpixel samples per
reference pixel, followed by sRGB encoding. The initial34cases cover textured
3D, dark3D, lineart2D, diagnostic frequency charts, gradients and embedded HUD,
multiple camera positions and2x/1.5x/3x scales. There are two full1080p output
sizes (960x540and1024x576inputs). Do not infer native-game/DLSS equivalence from
this small synthetic corpus. Point-sampled inputs can strongly alias, and a
chart intentionally contains unrecoverable frequencies.

The additional profile is generated with --holdout-poses --aa-input: four low-res
samples and previously unused poses in the same scene families. It tests a
different input-AA condition, not independent artistic/game content. Do not use
it to claim generalization across all games. Renderer/fixture/pixel hashes are
saved in manifest.json. The initial corpus is frozen in research/spatial-quality.

Reproduction (working directory E:/ThorNative, bundled Python with NumPy/Pillow):
  python experiments/spatial-quality/prepare.py research/spatial-quality
  python experiments/spatial-quality/test_metrics.py
  ./build.ps1
  ./experiments/spatial-quality/build.ps1 -DataDirectory E:/ThorNative/research/spatial-quality
  adb install -r build/spatial-quality/spatial-quality-qa.apk
  adb shell am instrument -r -w com.thor.nativefg.spatialqa/com.thor.spatialqa.SpatialInstrumentation
Save that exact instrumentation output to a UTF-8file, then:
  python experiments/spatial-quality/collect.py FIXTURE_DIR OUTPUT_DIR INSTRUMENTATION_FILE --bounded
  python experiments/spatial-quality/score.py FIXTURE_DIR OUTPUT_DIR --bounded --log OUTPUT_DIR/validation-log.txt

The temporary QA app has no Activity, overlay, root or input control. It enables
Khronos synchronization validation in its own VkInstance, requests no optional
float16/image features, and copies only bundled fixtures to its own data. Output
cleanup is restricted to known QA filenames in its own results folder. Each
run verifies the bundled fixture and extracted native library SHA before loading.
collect.py requires successful current-PID instrumentation, loaded Khronos layer,
zero VUID/sync/runtime errors and exactly the expected file names/sizes. Its tar
export uses run-as and binary-safe subprocess stdout, without general extraction.
Remove the temporary package after testing; do not change global GPU settings.

The CLI build/thor-spatial-lab FIXTURES.bin EXISTING_OUTPUT_DIR is available for
nonvalidation profiling. GPU timestamps cover the complete spatial record,
including crop/barriers/neural resampling, but exclude upload/readback, source
scene rendering, game rendering, presentation and input latency. There are five
warmups plus20timed submissions; inputs>=960wide also warm for at least250ms.
Non-bilinear model order rotates by case. These short headless costs are not
sustained thermal/FPS measurements. Do not confuse them with the older isolated
five-kernel Anime4K experiment timings.

Metrics are gamma RGB/BT.601Y PSNR (four-display-pixel border), Y SSIM with an
11x11Gaussian/sigma1.5and five-pixel border, and fixed edge/HUD errors. Aggregate
PSNR is derived from mean case MSE. SSIM moments use FP64 after a constant-image
control exposed FP32 cancellation. Six numeric controls are in test_metrics.py.
Unwarped temporal error-delta MAE includes motion/reconstruction differences; it
is not an isolated perceptual flicker score. Inspect fixed comparison crops as
well as numeric metrics. Alpha and independent GPU-vs-CPU bilinear are gates.

Models: bilinear, unchanged GSR1, raw Anime4K CNN S, and bounded neural. The
bounded variant clamps final RGB to the source bilinear footprint's per-channel
range, once at final resolution. Alpha is sampled independently. Exact integer
footprint arithmetic avoids odd-image center discontinuities. It does not add
motion/depth/jitter, temporal SR or NVIDIA technology. The raw network is kept
as an explicit control, not silently replaced in comparison data.

compare.py FIXTURE_DIR BASELINE_DIR CANDIDATE_DIR requires bit-exact output from
the three unchanged controls and exact CPU-defined final clamping for all pixels.
The baseline0.11APK/native/source fingerprints and original GPU output files
are retained in research/. Full product requirements remain outstanding in
PRODUCT-REQUIREMENTS.txt, including actual-game quality, temporal SR, GLES/all
emulators, fast cuts/HUD/occlusion and sustained/latency behavior.
