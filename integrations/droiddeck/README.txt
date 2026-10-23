GSR2 for DroidDeck — investigation and isolated test preparation

Pinned source: https://github.com/justradical/Sloptiscaler
Commit f80e4da339cd4624212fd0546922571fe01205c1 (release tag finally).
Release asset OptiScaler-SGSR2-arm64ec-f80e4da3.zip, 5742146 bytes.
SHA256 a520471c43118d0d07ab3264ede4f2dd11a94c1f6c5449b3635cf25a1a1c0a27
matches the GitHub release asset digest. GPL3 source, Qualcomm BSD3 shaders.

prepare-gsr2.py verifies the archive and prepares build/droiddeck-gsr2-test
with a dxgi proxy DLL, explicit SGSR2 settings, disabled third-party plugins
and disabled OptiScaler FG. It copies release documentation/licenses and
records file hashes. It does not install anything or modify a game/device.
This is the upstream release, not a locally rebuilt/patched binary.

Requires a Windows game supplying DLSS/FSR2+/XeSS inputs under compatible
ARM64EC Proton. A game offering only FSR1 or final framebuffer scaling cannot
supply those inputs. Choosing SGSR2 in this ini alone proves nothing. Need
actual SGSR2 init/evaluation logs, input/output dimensions and visual/pacing
checks in the chosen game. The release calls Vulkan untested; D3D11 uses the
D3D11-on-12 bridge, which adds another compatibility condition.

Kettle reference source cloned at 91e3b5cfe90925d469430fc915101df5579b4e68,
packages/optiscaler-arm64ec: same base plus kernel weight guard, display-size
motion-vector fixes and other patches. Those patches are NOT in the prepared
upstream release. Review/test them before trusting motion quality.

Initial device state: DroidDeck0.5.0 build3c5e95a, Linux runtime r9, session idle.
Initial inventory did not modify installed games. Existing Thor auto/manual SR off.
Cached Steam library includes BG3, Witcher3 and Cyberpunk IDs; cache presence
does not establish full ownership/library completeness or installed content.
Installed-game scan found no matching nvngx_dlss/libxess/ffx_fsr2_api/AMD
upscaler DLLs; static implementations are not ruled out by this filename scan.

User offered to download a small candidate. Selected Foxtale Demo, Steam
app3448970: free, official page mentions DLSS, 2GB required disk space (not a
verified download size), GTX1050 minimum. Actual Thor/Proton compatibility and
interceptable temporal API was initially untested. Baldur's Gate3 is a heavier fallback (official FSR2.2 support;
release author reports Adreno740 tests, not evidence from this device).

Current test update:
User installed Foxtale Demo (manifest StateFlags4, SizeOnDisk2338910871).
Verified actual game has DLSS nvngx_dlss.dll and XeSS libxess.dll plugins;
shipping exe is LittleFoxSim_v1/Binaries/Win64/LittleFoxSim_v1-Win64-Shipping.exe.
Prepared DLL machine verified COFF-ARM64EC(0xA641), SHA256
06616e8270cd2aa3cc819a5078b1116762e7c5536282a7b6caf0e13691913147.
Config forces SGSR2, disables OptiScaler FG/plugins, enables DXGI spoofing and
Info file logging to OptiScaler-Thor-GSR2.log. Config SHA256
f17ae55f5a9b91a1e16f469a889ef215ae5a4d41356601991ca21279b912b04c.
Both files copied into the shipping exe directory only after checking they
were absent and no shipping process ran. Rollback record is device-private
/data/adb/thor-native-fg/foxtale-gsr2-test-v1; install/undo scripts included here.
The user was asked to set WINEDLLOVERRIDES="dxgi=n,b" %command% in this game's
Steam launch options, then open and choose DLSS Quality if available.
Do not run another emulator game concurrently: actual Android LOW_MEMORY
incident killed the old Nightly session during the previous multi-game test.

First launch findings (research/OptiScaler-Thor-GSR2.log):
- Game launched on Wine 11.0 / Turnip Adreno 740 in D3D11 mode and called
  NVSDK_NGX_D3D11_CreateFeature / NVSDK_NGX_D3D11_EvaluateFeature.
- SGSR2_Dx11on12 initialized at 682x384 -> 1024x576 (Quality=2, AutoExposure=true,
  DepthInverted=true, IsHdr=true).
- All 6 D3D11->D3D12 shared textures (Color, MotionVectors, Depth, Output,
  Exposure, ReactiveMask) succeeded via CreateSharedHandle + OpenSharedHandle.
- Unpatched upstream release failed only at:
  EnsureSyncResourcesLocked Can't create shared handle for Dx11WithDx12 texture copy fence: 0
  because Wine/DXVK ID3D11Fence::CreateSharedHandle returns 0.

Patched ARM64EC rebuilds & live device verification (2026-10-23):
- Fixed integrations/droiddeck/build-arm64ec-windows.py to include weak undefined
  symbols ('w') in vulkan-1.def for ARM64EC COFF objects and invalidate stale .o
  files when headers change.
- Intermediate rebuild (SHA256 832870492792d5fc1d74fb81fe3c7f63127235b06aeb52e5f7fd98305c54f5b9)
  proved local fence fallback works and SGSR2_Dx11on12 ran every frame at
  853x480 -> 1280x720 (research/OptiScaler-Thor-GSR2-patched.log), but live
  screenshot (research/foxtale_sgsr2_live.png) exposed vertical green/purple
  tiled stripe corruption because DXVK (D3D11) and vkd3d-proton (D3D12)
  disagree on Turnip Adreno 740 UBWC/tile layout for CreateSharedHandle textures.
- Final Native D3D11 Compute SGSR2 rebuild:
  Rewrote OptiScaler/upscalers/sgsr2/SGSR2Feature_Dx11on12.{h,cpp} to inherit
  directly from IFeature_Dx11 (IsWithDx12() = false) and execute Qualcomm's
  2-pass cs_5_0 compute shaders (SGSR2_ConvertShader + SGSR2_UpscaleShader)
  natively on the game's ID3D11DeviceContext (DXVK), binding game SRV/UAV
  resources directly (copy=false) with zero vkd3d-proton device, zero cross-API
  shared handles, and zero CPU fence stalls.
- Final ARM64EC dxgi.dll SHA256:
  8832d615be41375ab85f5c1c7a122f2924fc71559cf47966a4704f14d781c4ff
- Live verification on AYN Thor (Foxtale Demo, Steam AppID 3448970):
  * research/OptiScaler-Thor-GSR2-dx11native.log confirms Native D3D11 SGSR2
    853x480 -> 1280x720 (Quality=2, color fmt 10 copy=false, mv 1280x720 fmt 34
    copy=false, depth 856x480 fmt 19 valid=true, output 1280x720 fmt 10 copy=false,
    motionVectorScale=(-0.0015625, 0.0027777778), preExposure=0.5683338).
  * Visual screenshots (research/foxtale_sgsr2_dx11native.png, foxtale_menu_fg.png,
    foxtale_gameplay_sgsr2_fg014.png) confirm clean, stripe-free 3D reconstruction
    and crisp HUD text in both title/menu and UE5 outdoor gameplay.
  * Simultaneous operation with ThorNativeLayer 0.14-rc1
    (com.thor.nativefg.runtime.v29e0b87f4308c5bb631f6edc9b5ae49e908c7f41ba99975f504e19b2d298e1d7):
    3D menu 59.32-60.36 source FPS -> 117.86-118.89 displayed FPS (gap median
    8.412ms, p95 8.415ms, fg_gpu_input ~1.73-1.84ms, fg_gpu_output ~0.64-0.66ms);
    heavy UE5 outdoor gameplay 14.26-16.72 source FPS -> 30.48-33.25 displayed FPS.

URLs:
https://store.steampowered.com/app/3448970/Foxtale_Demo/
https://baldursgate3.game/news/patch-4-now-live_96
https://github.com/justradical/Sloptiscaler/releases/tag/finally
https://github.com/kettlelinux/kettlelinux/tree/91e3b5cfe90925d469430fc915101df5579b4e68/packages/optiscaler-arm64ec
