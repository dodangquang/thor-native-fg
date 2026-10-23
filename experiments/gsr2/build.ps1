param([string]$DataDirectory='E:/ThorNative/research/gsr2-plane',[switch]$UseRelaxedPrecision,[switch]$FragmentMath,[switch]$StableKernel,[switch]$FullKernel,[switch]$ProductionEngine,[switch]$LifecycleGate)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$sdk='E:/DevTools/Android/Sdk'
$bt="$sdk/build-tools/35.0.0"
$jdk='C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot'
$cc="$sdk/ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android29-clang++.cmd"
$lab="$root/build/gsr2"
function Check([string]$stage){if($LASTEXITCODE){throw "$stage failed ($LASTEXITCODE)"}}
$fixture=(Resolve-Path -LiteralPath "$DataDirectory/fixtures.bin").Path
$fixtureHash=(Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash.ToLowerInvariant()
New-Item -ItemType Directory -Force -Path "$lab/lib/arm64-v8a","$lab/assets/legal/Anime4K","$lab/assets/legal/SGSR1","$lab/classes","$lab/dex" | Out-Null
$deviceSource=Get-Content -LiteralPath "$root/experiments/spatial-quality/lab.cpp" -Raw
$deviceSource=$deviceSource.Substring($deviceSource.IndexOf('struct Device {'),$deviceSource.IndexOf('struct Fixture {')-$deviceSource.IndexOf('struct Device {'))
if($LifecycleGate){
 if(-not $ProductionEngine){throw 'LifecycleGate requires ProductionEngine'}
 $gateFeature=@'
VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};VkPhysicalDeviceFeatures2 available{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};available.pNext=&timeline;vkGetPhysicalDeviceFeatures2(gpu,&available);if(!timeline.timelineSemaphore)throw std::runtime_error("Timeline test gate unsupported");timeline.timelineSemaphore=VK_TRUE;const char* timelineExtension=VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;di.pNext=&timeline;di.enabledExtensionCount=1;di.ppEnabledExtensionNames=&timelineExtension;
'@
 $deviceSource=$deviceSource.Replace('requireVk(vkCreateDevice(gpu,',$gateFeature+"`n  requireVk(vkCreateDevice(gpu,")
}
[IO.File]::WriteAllText("$lab/lab_device.hpp",$deviceSource,[Text.UTF8Encoding]::new($false))
$shaderBin="$sdk/ndk/27.3.13750724/shader-tools/windows-x86_64"
$header="#pragma once`n#include <cstdint>`n"
if($FragmentMath){
 if($UseRelaxedPrecision){throw 'Fragment-math experiment requires full precision'}
 $adapterFlags=@();if($StableKernel){$adapterFlags+='--stable'}
 if($FullKernel){$adapterFlags+='--full'}
 & 'C:/Users/dodan/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' "$root/scripts/prepare-temporal-shaders.py" "$PSScriptRoot/upstream" $lab @adapterFlags;Check 'Fragment math adapter'
}
elseif($StableKernel){throw 'StableKernel requires FragmentMath'}
if($FullKernel -and -not $FragmentMath){throw 'FullKernel requires FragmentMath'}
foreach($name in @('convert','upscale')){
 if(-not $FragmentMath){
 $shader=Get-Content -LiteralPath "$PSScriptRoot/upstream/sgsr2_$name.comp" -Raw
 $shader=$shader.Replace('#version 320 es','#version 450')
 if(-not $UseRelaxedPrecision){$shader=$shader.Replace('mediump','highp')}
 # Upstream assumes dispatch dimensions divisible by8. Guard padded workgroups.
 $limit=if($name -eq 'convert'){'renderSize'}else{'displaySize'}
 $shader=$shader.Replace("void main()`n{","void main()`n{`n    if(any(greaterThanEqual(gl_GlobalInvocationID.xy, params.$limit))) return;")
 $shader=$shader.Replace("void main()`r`n{","void main()`r`n{`r`n    if(any(greaterThanEqual(gl_GlobalInvocationID.xy, params.$limit))) return;")
 if($name -eq 'upscale'){
  # texelFetch bypasses sampler address modes; explicitly bound edge reads.
  $shader=$shader.Replace('ivec2 InputPos = ivec2(Jitteruv * InputInfoViewportSize);','ivec2 InputPos = clamp(ivec2(Jitteruv * InputInfoViewportSize), ivec2(0), ivec2(params.renderSize)-1);')
  $shader=$shader.Replace('texelFetch(YCoCgColor, InputPosBtmRight, 0)','texelFetch(YCoCgColor, clamp(InputPosBtmRight, ivec2(0), ivec2(params.renderSize)-1), 0)')
 }
 $shader=[regex]::Replace($shader,'layout\(binding = ([01]),', {param($m)'layout(binding = '+(10+[int]$m.Groups[1].Value)+','})
 [IO.File]::WriteAllText("$lab/$name.comp",$shader,[Text.UTF8Encoding]::new($false))
 }
 & "$shaderBin/glslc.exe" --target-env=vulkan1.1 -O "$lab/$name.comp" -o "$lab/$name.spv";Check "GSR2 compile $name"
 & "$shaderBin/spirv-val.exe" --target-env vulkan1.1 "$lab/$name.spv";Check "GSR2 SPIRV $name"
 $bytes=[IO.File]::ReadAllBytes("$lab/$name.spv");$words=for($i=0;$i -lt $bytes.Length;$i+=4){'0x'+[BitConverter]::ToUInt32($bytes,$i).ToString('x8')+'u'}
 $header+='static const uint32_t '+$name+'_spv[]={' + ($words -join ',') + "};`n"
}
[IO.File]::WriteAllText("$lab/gsr2_shaders.hpp",$header,[Text.UTF8Encoding]::new($false))
& "$root/scripts/prepare-temporal-shaders.ps1" -Destination "$root/build/generated"
$engineFlags=@();if($ProductionEngine){$engineFlags+='-DTHOR_GSR2_PRODUCTION'}
if($LifecycleGate){$engineFlags+='-DTHOR_GSR2_LIFECYCLE'}
& $cc -std=c++17 -O2 -fPIC -shared -static-libstdc++ @engineFlags -I "$root/native" -I "$root/tests" -I "$root/build/generated" -I "$lab" "$root/native/engine.cpp" "$root/native/temporal.cpp" "$PSScriptRoot/lab.cpp" -lvulkan -llog '-Wl,-z,max-page-size=16384' -o "$lab/lib/arm64-v8a/libthor_gsr2_lab.so";Check 'GSR2 lab JNI'
$nativeHash=(Get-FileHash -LiteralPath "$lab/lib/arm64-v8a/libthor_gsr2_lab.so" -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceHashes=@{};foreach($path in @('native/engine.cpp','native/engine.hpp','build/generated/shaders.hpp','build/gsr2/gsr2_shaders.hpp','build/gsr2/lab_device.hpp','experiments/gsr2/lab.cpp','experiments/gsr2/build.ps1','experiments/gsr2/prepare.py','experiments/gsr2/scene3d.py','experiments/gsr2/test_fixture.py','scripts/prepare-temporal-shaders.py','experiments/gsr2/upstream/sgsr2_convert.comp','experiments/gsr2/upstream/sgsr2_upscale.comp','experiments/gsr2/upstream/sgsr2_convert.fs','experiments/gsr2/upstream/sgsr2_upscale.fs')){$sourceHashes[$path]=(Get-FileHash -LiteralPath "$root/$path" -Algorithm SHA256).Hash.ToLowerInvariant()}
foreach($path in @('native/temporal.cpp','native/temporal.hpp','native/temporal_sequence.hpp','native/vkapi.hpp','scripts/prepare-temporal-shaders.ps1','build/generated/temporal_shaders.hpp','third_party/sgsr2/sgsr2_convert.fs','third_party/sgsr2/sgsr2_upscale.fs')){$sourceHashes[$path]=(Get-FileHash -LiteralPath "$root/$path" -Algorithm SHA256).Hash.ToLowerInvariant()}
$sourceHashes['tests/temporal_gpu_test.hpp']=(Get-FileHash -LiteralPath "$root/tests/temporal_gpu_test.hpp" -Algorithm SHA256).Hash.ToLowerInvariant()
$provenance=@{fixtures_sha256=$fixtureHash;native_sha256=$nativeHash;source_hashes=$sourceHashes;upstream_commit='d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e';relaxed_precision=[bool]$UseRelaxedPrecision;fragment_math_as_compute=[bool]$FragmentMath;stable_kernel=[bool]$StableKernel;full_kernel=[bool]$FullKernel;production_engine=[bool]$ProductionEngine;lifecycle_test_timeline_feature=[bool]$LifecycleGate}
[IO.File]::WriteAllText("$lab/assets/provenance.json",($provenance | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath "$root/research/validation/android-binaries-1.4.363.0/arm64-v8a/libVkLayer_khronos_validation.so" -Destination "$lab/lib/arm64-v8a/libVkLayer_khronos_validation.so" -Force
Copy-Item -LiteralPath "$sdk/ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" -Destination "$lab/lib/arm64-v8a/libc++_shared.so" -Force
Copy-Item -LiteralPath $fixture -Destination "$lab/assets/fixtures.bin" -Force
Copy-Item -LiteralPath "$DataDirectory/manifest.json" -Destination "$lab/assets/manifest.json" -Force
Copy-Item -LiteralPath "$root/third_party/anime4k/LICENSE.txt","$root/third_party/anime4k/Anime4K_Upscale_CNN_x2_S.glsl" -Destination "$lab/assets/legal/Anime4K" -Force
Copy-Item -LiteralPath "$root/third_party/Snapdragon-GSR-LICENSE.txt","$root/third_party/sgsr1_shader_mobile.frag" -Destination "$lab/assets/legal/SGSR1" -Force
New-Item -ItemType Directory -Force -Path "$lab/assets/legal/SGSR2" | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/upstream/LICENSE","$PSScriptRoot/upstream/sgsr2_convert.comp","$PSScriptRoot/upstream/sgsr2_upscale.comp","$PSScriptRoot/upstream/sgsr2_convert.fs","$PSScriptRoot/upstream/sgsr2_upscale.fs" -Destination "$lab/assets/legal/SGSR2" -Force
$manifest='<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.thor.nativefg.gsr2qa" android:versionCode="1"><uses-sdk android:minSdkVersion="29" android:targetSdkVersion="33"/><application android:debuggable="true" android:extractNativeLibs="true" android:allowBackup="false" android:label="Thor Spatial Quality Lab"/><instrumentation android:name="com.thor.gsr2qa.SpatialInstrumentation" android:targetPackage="com.thor.nativefg.gsr2qa"/></manifest>'
[IO.File]::WriteAllText("$lab/AndroidManifest.xml",$manifest,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText("$lab/LabFixture.java",'package com.thor.gsr2qa; final class LabFixture { static final String SHA256="'+$fixtureHash+'"; static final String NATIVE_SHA256="'+$nativeHash+'"; }',[Text.UTF8Encoding]::new($false))
& "$jdk/bin/javac.exe" -encoding UTF-8 -source 8 -target 8 -bootclasspath "$sdk/platforms/android-34/android.jar;$bt/core-lambda-stubs.jar" -d "$lab/classes" "$PSScriptRoot/SpatialInstrumentation.java" "$lab/LabFixture.java";Check 'Spatial instrumentation Java'
$classes=@(Get-ChildItem -LiteralPath "$lab/classes" -Recurse -Filter '*.class' | ForEach-Object FullName)
& "$bt/d8.bat" --lib "$sdk/platforms/android-34/android.jar" --min-api 29 --output "$lab/dex" @classes;Check 'Spatial instrumentation dex'
& "$bt/aapt.exe" package -f -M "$lab/AndroidManifest.xml" -A "$lab/assets" -I "$sdk/platforms/android-34/android.jar" -F "$lab/unsigned.apk";Check 'Spatial APK assets'
& "$jdk/bin/jar.exe" uf "$lab/unsigned.apk" -C $lab lib -C "$lab/dex" classes.dex;Check 'Spatial APK code'
& "$bt/zipalign.exe" -f -p 4 "$lab/unsigned.apk" "$lab/aligned.apk";Check 'Spatial APK alignment'
& "$bt/apksigner.bat" sign --ks "$root/build/thor-native-development.jks" --ks-key-alias thor --ks-pass pass:android --key-pass pass:android --out "$lab/gsr2-qa.apk" "$lab/aligned.apk";Check 'Spatial APK signing'
& "$bt/apksigner.bat" verify "$lab/gsr2-qa.apk";Check 'Spatial APK signature'
Write-Output "SPATIAL_LAB_BUILD fixtures_sha256=$fixtureHash apk=$lab/gsr2-qa.apk"
