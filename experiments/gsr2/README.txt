SGSR2 Vulkan temporal reconstruction experiment (not installed in the controller)

Upstream: SnapdragonGameStudios/snapdragon-gsr, commit
d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e, sgsr/v2/include/glsl_2_pass_cs.
The two unmodified upstream files and BSD3license are in upstream/.
No external artwork, game assets or dataset is used by prepare.py.

The QA APK invokes both compute passes on Thor/stock Adreno with its own
Khronos synchronization validation, no optional float16 features, global GPU
settings, root, overlays, input actions or game configuration changes. It is a
separate temporary package com.thor.nativefg.gsr2qa; uninstall after collection.
The Java wrapper checks exact fixture and extracted native-library SHA256.
Provenance binds sources/generated shaders and the selected precision adapter.

Adapters in build.ps1: Vulkan450; storage bindings10/11 to avoid OpenGL namespace
collisions; highp instead of mediump; padded-dispatch guards; bounded texelFetch
at the right/bottom edge (sampler clamp does not apply to texelFetch).
The original sources remain unchanged. -UseRelaxedPrecision reproduces the
precision experiment; it is not the recommended path on this Thor.

Reproduction from E:/ThorNative with bundled NumPy/Pillow Python:
 python experiments/gsr2/prepare.py research/gsr2-still-camera
 ./experiments/gsr2/build.ps1 -DataDirectory E:/ThorNative/research/gsr2-still-camera
 adb install -r build/gsr2/gsr2-qa.apk
 adb shell am instrument -r -w com.thor.nativefg.gsr2qa/com.thor.gsr2qa.SpatialInstrumentation
 Save exact instrumentation text as a UTF8file, then:
 python experiments/gsr2/collect.py FIXTURE_DIR OUTPUT_DIR INSTRUMENTATION_FILE
 adb uninstall com.thor.nativefg.gsr2qa
 For1080p add --width 960 --height 540.
 For odd extents use --width 130 --height 74; for constant controls add
 --constant-control. --history-disabled resets every frame; --zero-velocity
 supplies deliberately incorrect zero object motion as a negative control.

The renderer supplies exact object velocity, normal-Z depth, identity camera,
Halton jitter and scene-reset flags. The camera-still field is1, independently
of moving objects; earlier research/gsr2-plane runs incorrectly used0 and are
retained as historical experiments, not current camera-provider evidence.
Two moving planes and a hard reset are deliberately narrow tests. No general
3D depth reprojection, reverse-Z, transparency, exposure changes, native-game
provider, combined FG, or renderer resize/recreation has been established.

collect.py requires exact run identity, Khronos load, no VUID/sync/fatal errors,
all32outputs, finite FP16RGB and exact output member sizes. It preserves raw
results and fixed reference/bilinear/GSR2 comparison PNGs. Eight-pixel borders
are excluded from PSNR; full-edge correctness is covered separately by audit.py.
PSNR is aggregate linear RGB on frame4..15and20..31; null PSNR means exact/
infinite with MSE0. This score is not a perceptual or native-game quality claim.
Timing brackets two GPU passes and barriers, excluding upload/readback, game,
display, source CPU and input latency. No GPU clock was locked or recorded in
these runs; do not interpret cross-run timings as controlled performance wins.

audit.py checks initial input bytes exactly, packed YCoCg against independent
CPU math, motion/exposure, bit-exact reset0/16against per-frame-reset output,
and constant output against upstream's inverse-tonemap formula. The upstream
600/65504term changes constant colors slightly; original-input error remains
reported, not silently removed from quality scores. These controls do not make
an unfavorable reconstruction-quality result a passing product result.

Current phone controller stays0.12, automaticOFF. These experiments do not add
a GSR2 selector or pretend color-only compositor access supplies depth/motion.
Full independent SR+FG product requirements remain in PRODUCT-REQUIREMENTS.txt.

Second executed checkpoint (see STATUS.txt):
- Scene revision2 fixes a material/vector inconsistency in the rectangle from
  revision1: object texture now moves with geometry. test_fixture.py verifies
  visible material advection, independent bilinear/jitter controls, perspective
  camera reprojection/column-major serialization, and normal depth analytically.
  Keep revision1datasets frozen; they do not prove exact material-flow semantics.
- --scene3d renders ground, shaded spheres and thin bars with a lateral camera,
  actual perspective Vulkan0..1depth and zero object-motion sentinel. Scene reset
  at16 changes camera/color. Reference uses4subpixel samples, source1. This is
  one authored scene family and does not establish real-game compatibility.
- -FragmentMath adapts the official2-pass fragment algorithm to compute, using
  a shared144byte validated UBO. Both stages still execute on GPU. This does NOT
  measure a native fragment/render-pass implementation's tile/performance benefit.
- -StableKernel caps the pre-scale radial kernel factor at1. Original fragment
  math at2x can have negative normalization mass and a reset alpha clamped to0.
  kernel_probe.py independently reproduces reset-frame GPU pixels in a flat-depth
  ROI within0.125byte and scans513x513phases. No tolerance was loosened. The
  stable factor gives strictly positive mass over that sampled grid. Raw variant
  remains a control. This is a Thor modification, not unchanged Qualcomm math.
- -FullKernel re-enables the upstream corner samples for a still camera. It was
  worse than the stable5tap variant in the2Dcase; retain as an explicit ablation,
  not the selected candidate. Camera-moving3D cases use the5tap branch.
- audit_camera.py checks frame1GPU motion against fixture matrices/depth. It
  explicitly models the fragment gather footprint rows-1,0,+2, which differs
  from the compute variant's symmetric3x3. Do not silently substitute footprints.
  The current tests cover lateral camera/static geometry and normal-Z only.
- compare_controls.py requires bit-identical paired inputs/references and same
  native library for history-off/bad-motion controls, plus bit-exact resets0/16.
  The stable candidate beats both controls and bilinear in the3Dscene at256x144
  output, and beats bilinear at512x288output. It still loses to bilinear in the
  moving2Dscene. No universal quality winner is claimed; no game selector added.

Reusable renderer backend is now native/temporal.cpp + temporal.hpp, with the
explicit input/queue/fence contract in native/TEMPORAL-INTEGRATION.txt. The shader
adapter moved to scripts/prepare-temporal-shaders.py; production pinned originals
are in third_party/sgsr2. Existing experiment originals remain frozen.
-ProductionEngine runs the new backend instead of the prototype. -LifecycleGate
adds a timeline semaphore ONLY to the test device to hold real GPU submissions
pending for slot/fence tests. Production backend requires no timeline extension.
Featureless and gated runs are recorded separately; do not merge their scopes.
New native backend32-frame outputs match the prototype exactly, including1.875x.
--output-width/--output-height select other fixture dimensions (3Dpair must keep
the same aspect). This remains a source/static renderer integration, not an
installed emulator provider or an automatic GSR2 game-activation feature.
