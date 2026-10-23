Eden low-resolution compositor provider — LOCAL EXPERIMENT, not temporal SR

Base: https://git.eden-emu.dev/eden-emu/eden.git
Commit: 989768aa46720e3fcab9e2111e3ddd70819c382b
Provider patch: thor-spatial-provider.patch (includes both new headers).
No upstream PR, push or submission is authorized or performed.
Eden is GPL-3.0-or-later; retain its source and license with any distribution.
This local experiment was prepared with AI assistance, not reviewed by Eden.

Purpose:
Composite a single opaque SDR guest framebuffer at its actual pixel density,
then expose the low-resolution -> Android swapchain blit to Thor's independent
spatial upscaler. Thor FG still runs in Thor's layer. This provider does not
extract depth, object motion vectors, projection jitter or a temporal reset
contract; it cannot enable GSR2 or NVIDIA DLSS.

Activation requires Thor manual SR targeting this exact process/package, or
the injected automatic-match environment plus enabled automatic SR. Otherwise
Eden uses its existing compositor. Built-in Eden FG disables this provider.
Unsupported crop/rotation/multiple layers/HDR/non-blit cases fall back to the
existing compositor. OpenGL is not modified.

Geometry checks: tests/composite_layout_test.cpp in the Thor repository;
12 concrete cases and 125 invariant cases passed on the Thor. These checks
do not prove actual GPU upscaling, visual quality or performance.

Toolchain used locally:
Android NDK 28.2.13676358, SDK 36, CMake 3.31.6, Java 17, Gradle 8.13,
Portable Git 2.56.0.3 (GNU patch 2.8), Khronos glslang 16.6.0.
build-provider.ps1 records local paths and the same CMake cache arguments for
both native and APK stages. It does not install or configure a device.
Expected test package: dev.eden.eden_emulator.relWithDebInfo; verify the built
manifest before installing. Never replace the user's live Nightly package or
restore old save archives. Keep new test data isolated from human progress.

Validation update: native build SUCCESSFUL (25m16s); APK assembly SUCCESSFUL
(2m45s). APK SHA256:
873357b6f3b214d01df4246fbd6d01b1b84bb18216f5ef1e4f97333e54979cff.
Manifest package verified dev.eden.eden_emulator.relWithDebInfo, minSdk33,
version989768a-relWithDebInfo. Installed beside Nightly; no game launched yet.

Temporal-input investigation update:
The patch now includes thor_temporal_probe.h and three opt-in call sites.
The probe records actual draw framebuffer/color/depth handles (as hashes),
extents, formats, sample count and depth test/write draw counts, correlating
them with the guest color image seen by the compositor. No resource handles
are dereferenced after draw observation or retained for GPU consumption.
This is metadata, not a validated depth/MV provider. It does not identify
projection matrices, motion-vector semantics, normal/reverse Z or HUD masks.
Set debug.thorfg.probe to a new nonzero integer token after entering a scene
to record up to128 composite frames, reporting every60 frames. At most256
distinct framebuffer candidates are retained per frame; overflow is logged.
Set the property0 to disable. Logging disturbs performance, so do not use
probe-on FPS as a performance benchmark. It makes no rendering changes.

Probe v2 build/assembly SUCCESSFUL40s, APK SHA256:
ece920d12c69cf6614e4d36c362b5ce571add87b242fc5c1bcd7efbc8c46177d.
Installed only to the research package. Keys/firmware and driver were copied
locally from the existing installation; config paths were rewritten to the
research package. No user saves or44GB user content copied/restored. Original
Nightly PID20692 preserved. The user completed Switch library setup manually.
Mario trace17 captured1600x900 final color and a640x720 render area with5
color targets and D32_FLOAT depth. V2 capacity32 overflowed163draws per sampled
frame, and the final snapshot was incompletely flushed at pull. No direct
final-color match was observed in the incomplete target inventory; this does
not establish absence of such a pass. SNORM two-channel buffers are not proven
motion vectors. parse-temporal-probe.py records these evidence limitations.

Probe v3 compiled successfully69s and installed while user had exited Mario.
APK SHA256 c386c96a4293b7375d5a7b9d7779e5b362e5f754b132ea193251661a99acf753.
V3 records per-view extents for color/depth and has capacity256,3 snapshots.

Live v3 probe & integrated SR+FG 0.14-rc1 verification (2026-10-23):
- Captured Probe v3 trace 18 in Super Mario Odyssey 3D Cap Kingdom gameplay
  (research/eden-mario-probe-18-full.log, research/eden-mario-probe-18-summary.json):
  all 3 snapshots (frames 0, 60, 120) completed with targets=57, overflow_draws=0,
  parsed_complete=True, and direct_final_matches=1 (1600x900 final color target
  plus 640x720 D32_FLOAT 5-color MRT pass with 849 draws). Probe property restored to 0.
- Verified end-to-end THOR_LOW_COMPOSITE + ThorNativeLayer 0.14-rc1
  (com.thor.nativefg.runtime.v29e0b87f4308c5bb631f6edc9b5ae49e908c7f41ba99975f504e19b2d298e1d7)
  in Super Mario Odyssey (research/eden-mario-sr-fg014.log,
  research/eden_mario_sr_fg014.png, research/eden_mario_3d_live.png):
  * YuzuNative logs THOR_LOW_COMPOSITE 1280x720 -> 1920x1080.
  * ThorNativeLayer reports sr=1 sa=2 sr_in_w=1280 sr_in_h=720 sr_presents=34055
    (bounded Neural 2D SR reconstructing 1280x720 -> 1920x1080 on every frame).
  * Title/menu scene (async_shared): 59.93-60.07 source FPS -> 117.87-118.89
    displayed FPS (gap median 8.412ms, p95 8.414ms, fg_gpu_input ~0.99-1.05ms,
    fg_gpu_output ~0.42-0.44ms).
  * 3D Cap Kingdom gameplay (fence default): 48.48-49.43 source FPS ->
    91.66-96.68 displayed FPS (gap median 8.415ms, p95 16.831ms, fg_gpu_input
    ~1.01-1.03ms, fg_gpu_output ~0.44-0.45ms).
  * Config and all debug.thorfg.* properties restored to 0 after testing.

Memory incident: original Nightly PID20692 was killed by Android low-memory
killer at17:49:32 while Mario and Steam were active, then its service restarted
as9948. dumpsys exit-info confirms LOW_MEMORY, not an intentional force-stop.
The user was informed; unsaved RAM progress may have been lost. No stale save
archive was restored. Avoid simultaneous emulator game sessions from now on.
THOR_LOW_COMPOSITE is a provider log, not proof Thor SR executed. Require
matching-process Thor SR counters, source/output extents, actual presentation
and visual comparisons before claiming an integrated result.
