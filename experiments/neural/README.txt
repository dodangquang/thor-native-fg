Thor neural super-resolution experiments — not yet integrated into the APK

The installed controller/engine remains version0.9 (native EA47E70F...). These
standalone probes do not inject a neural model into games or change device
settings. The full DLSS-like product objective remains in PRODUCT-REQUIREMENTS.

Reproduce the host build on the existing Windows/Android toolchain:
  ./build.ps1
  ./experiments/neural/setup.ps1
  ./experiments/neural/build.ps1
setup downloads SHA256-pinned weights and AARs into research/, extracts runtime
libraries and their license notices into E:/DevTools/ThorML, and prepares the
32x32 CPU references. It installs no Android APK and changes no boot/settings.
Use the bundled Python with E:/DevTools/ThorML/python on PYTHONPATH for the
export/verification scripts. CPU host inference is a conversion reference only.

Thor experiments use /data/local/tmp/thor-neural for executables, runtime libs
and fixtures. The ONNX runner needs that directory plus /vendor/lib64 in its
process-local LD_LIBRARY_PATH, and HTP needs a process-local ADSP_LIBRARY_PATH.
Run thor-onnx-probe MODEL htp|gpu|cpu [INPUT.f32 OUTPUT.f32 PROFILE_PREFIX]. HTP
and GPU modes explicitly disable the CPU execution-provider fallback. Their
runtime diagnostics go to logcat; ORT_SESSION_BEGIN prints the actual PID. The
QNN GPU backend is not the direct Vulkan implementation. Do not confuse them.

Run the direct Vulkan probe with:
  thor-neural-vulkan quicksrnet-packed.bin 960 540
  thor-neural-vulkan anime4k 960 540
For image comparisons append INPUT.rgba8 OUTPUT.rgba8. For the QuickSRNet naive
control append 'naive' after both image paths. The default QuickSRNet shader is
the shared-tile candidate, which measured SLOWER than naive; it is retained as
a failed optimization, not selected for production. Both output comparisons
pass. No neural path is enabled in the installed engine by these tools.

Sources/provenance:
- QuickSRNet Small2x: Qualcomm AIMET Model Zoo, BSD-3-Clause. Architecture has
  four3x3 convolutions,32channels and two intermediate layers, clipped hidden
  and output activations, and PixelShuffle2x. The exact checkpoint URL and
  SHA are in export_quicksrnet.py. Only storage/tensor/container symbols from
  that frozen checkpoint are accepted; torch/optimizer code is not imported.
  https://github.com/quic/aimet-model-zoo/tree/develop/aimet_zoo_torch/quicksrnet
  https://github.com/quic/aimet-model-zoo/blob/develop/LICENSE.md
- The ONNX export reorders final channels from PyTorch CRD to QNN DCR and
  matches independent direct-convolution/CRD reference math within1.5e-6.
  Bounded uint8 activation/per-channel int8 weight quantization is a candidate,
  not a quality-approved model: the32x32 fixture differs by0.132max/0.029MAE.
- ONNX Runtime1.22.0 QNN AAR (Microsoft) and QNN runtime2.33.0 AAR (Qualcomm)
  are fetched from Maven Central. SDK runtime binaries are not committed or
  shipped in Thor0.9. Keep their extracted license notices when experimenting;
  redistribution/integration has not been implemented by this change.
  https://onnxruntime.ai/docs/execution-providers/QNN-ExecutionProvider.html
- Anime4K CNN S: original source and MIT license are in third_party/anime4k.
  The model is specialized for line art, not proven for every game. Four
  convolutions produce4-feature tensors with positive/negative branches, then
  a learned residual is shuffled2x and added to a bilinear base. The Vulkan
  adapter preserves source alpha instead of adding the residual to alpha.
  It is still single-image SR, not temporal/neural DLSS reconstruction.

Evidence from the actual Thor, 2026-10-23:
- NNAPI exposes only nnapi-reference CPU. Device ro.soc.model=QCS8550,
  soc_id=603. The cDSP device-tree node status is the bytes 'no\0'; cDSP is
  not in the remoteproc inventory. QNN HTP2.33 fails transport/device creation
  with14001, remote_get_info ENODEV, in shell and root. This is evidence about
  this firmware/runtime path, not proof of absent silicon or all UID contexts.
  No kernel, DT, firmware, SELinux or DSP power configuration was modified.
- QuickSRNet on QNN GPU,960x540->1920x1080: FP32 median75.074ms, FP16 48.218ms
  for20runs after5warmups. CPU fallback is off. A32x32 profile contains only
  QNNExecutionProvider compute events; FP32 readback max error1.13e-6 against
  the independent reference. FP16 max error0.00816. These are implementation
  accuracy comparisons, not SR-quality PSNR against native high-res content.
- Direct Vulkan QuickSRNet, same dimensions: naive212.398ms, shared286.783ms.
  Reject both for realtime game integration. Tiling alone did not help.
- Direct Vulkan Anime4K CNN S:540p->1080p 1.6428ms;720p->1440p 2.8900ms.
  GPU timestamps cover four convolutions and shuffle, with>=1second warmup,
  then20samples. They exclude source upload/readback, crop,1.5x resampling,
  game rendering, presentation and latency. Source stays GPU-resident during
  inference.32x32 readback matches the CPU matrix reference to at most1byte
  after RGBA8 conversion; source alpha is preserved in this opaque fixture.
  Gameplay quality, non-opaque alpha/crops/resize/lifecycle and combined FG
  cost remain unverified. These promising kernel times are not game FPS.
- research/neural-output-verification.json and the per-model probe logs retain
  all good and bad results. verify_outputs.py reproduces the reference checks.

Next: integrate the lightweight model as a clearly named optional SR path,
verify independent reference pixels/alpha/crops/resize, compare to bilinear and
SGSR1 on native-ground-truth game-like fixtures, then test simultaneous SR+FG
in actual games. Keep ordinary3D quality, temporal SR, fast cuts, GLES, emulator
coverage and sustained/latency evidence in scope. Do not substitute an anime
upscaler benchmark for completion of the requested general product.
