param([string]$Sdk='E:/DevTools/Android/Sdk')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$ndk=Join-Path $Sdk 'ndk/27.3.13750724'
$bin=Join-Path $ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin'
$shaderBin=Join-Path $ndk 'shader-tools/windows-x86_64'
$jdk='E:/DevTools/Java/jdk-17.0.20.101-hotspot'
if(-not(Test-Path -LiteralPath "$jdk/bin/javac.exe")){$jdk='C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot'}
$env:JAVA_HOME=$jdk
$bt=Join-Path $Sdk 'build-tools/35.0.0'
$androidJar=Join-Path $Sdk 'platforms/android-34/android.jar'
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path "$build/lib/arm64-v8a","$build/classes","$build/dex","$build/generated" | Out-Null
function Check([string]$stage){if($LASTEXITCODE -ne 0){throw "$stage failed ($LASTEXITCODE)"}}
# Adapt the upstream BSD-3-Clause SGSR1 fragment entry point to Vulkan compute.
# The filtering math is kept, and the original source and license are shipped.
$shader=Get-Content -LiteralPath "$root/third_party/sgsr1_shader_mobile.frag" -Raw
$start=$shader.IndexOf('float fastLanczos2')
$shader=$shader.Substring($start).Replace('highp ','').Replace('in_TEXCOORD0.xy','uv').Replace('in_TEXCOORD0.x','uv.x').Replace('in_TEXCOORD0.y','uv.y')
$shader=$shader.Replace('void main()','void main()').Replace('int mode = OperationMode;', 'ivec2 at=ivec2(gl_GlobalInvocationID.xy); if(any(greaterThanEqual(at,p.outputSize)))return; vec2 uv=(vec2(at)+0.5)/vec2(p.outputSize); vec2 sourceSize=vec2(textureSize(ps0,0)); vec4 ViewportInfo[1]; ViewportInfo[0]=vec4(1.0/sourceSize,sourceSize); int mode = OperationMode;')
$shader=$shader.Replace('int mode = OperationMode;','const int mode = OperationMode;').Replace('out_Target0.xyzw = color;','imageStore(result,at,color);').Replace('float std = 2.181818/sum;','float std = 2.181818/max(sum,0.000001);').Replace('float finalY = aWY.y/aWY.x;','float finalY = aWY.y/(abs(aWY.x)<0.000001?0.000001:aWY.x);')
$shader=$shader.Replace('color.w = 1.0;', 'color.w = textureLod(ps0,uv,0.0).a;')
$prefix=@'
#version 450
// SGSR1 filtering: Copyright (c) 2025 Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSD-3-Clause. Vulkan compute adapter: Thor Native FG.
#define OperationMode 1
#define EdgeThreshold (8.0/255.0)
#define EdgeSharpness 2.0
layout(local_size_x=8,local_size_y=8) in;
layout(binding=0) uniform sampler2D ps0;
layout(binding=1,rgba8) uniform writeonly image2D result;
layout(push_constant) uniform Params { ivec2 outputSize; } p;
'@
[IO.File]::WriteAllText("$build/generated/upscale.comp",$prefix+"`n"+$shader,[Text.UTF8Encoding]::new($false))
& "$root/scripts/prepare-neural-shaders.ps1" -Destination "$build/generated"
$header='#pragma once' + "`n#include <cstdint>`n"
foreach($name in @('analyze','quality','flow','flow_reference','interpolate','upscale','scene','neural0','neural1','neural2','neural3','neural4','neural5')){
 $src=if($name -eq 'neural5'){"$root/shaders/neural_resize.comp"}elseif($name -eq 'neural4'){"$root/shaders/neural_shuffle.comp"}elseif($name -eq 'upscale' -or $name -match '^neural[0-3]$'){"$build/generated/$name.comp"}elseif($name -eq 'flow_reference'){"$root/tests/reference/flow_serial.comp"}else{"$root/shaders/$name.comp"}
 & "$shaderBin/glslc.exe" --target-env=vulkan1.1 -I "$root/shaders" -O $src -o "$build/$name.spv";Check "glslc $name"
 & "$shaderBin/spirv-val.exe" --target-env vulkan1.1 "$build/$name.spv";Check "spirv-val $name"
 $bytes=[IO.File]::ReadAllBytes("$build/$name.spv")
 $words=for($i=0;$i -lt $bytes.Length;$i+=4){'0x'+[BitConverter]::ToUInt32($bytes,$i).ToString('x8')+'u'}
 $header+='static const uint32_t '+$name+'_spv[]={' + ($words -join ',') + "};`n"
}
[IO.File]::WriteAllText("$build/generated/shaders.hpp",$header,[Text.UTF8Encoding]::new($false))
$cc=Join-Path $bin 'aarch64-linux-android29-clang++.cmd'
$common=@('-std=c++17','-O2','-fPIC','-I',"$build/generated",'-I',"$root/native",'-static-libstdc++')
& $cc @common -shared "$root/native/engine.cpp" "$root/native/layer.cpp" -o "$build/lib/arm64-v8a/libVkLayer_ThorFG.so" -llog '-Wl,-Bsymbolic' '-Wl,-z,max-page-size=16384';Check 'Vulkan layer'
& $cc @common -shared "$root/native/engine.cpp" "$root/native/demo.cpp" -o "$build/lib/arm64-v8a/libthor_demo.so" -landroid -llog -lvulkan '-Wl,-z,max-page-size=16384';Check 'Vulkan demo'
& $cc @common "$root/native/engine.cpp" "$root/native/selftest.cpp" -o "$build/thor-selftest" -lvulkan -llog;Check 'GPU selftest'
& $cc @common "$root/native/engine.cpp" "$root/native/neural_selftest.cpp" -o "$build/thor-neural-selftest" -lvulkan -llog;Check 'Neural spatial selftest'
& $cc @common "$root/native/engine.cpp" "$root/native/benchmark.cpp" -o "$build/thor-gpu-bench" -lvulkan -llog;Check 'GPU stage benchmark'
& $cc @common "$root/tests/presentation_test.cpp" -o "$build/thor-presentation-test";Check 'Presentation timing selftest'
& $cc @common "$root/tests/auto_config_test.cpp" -o "$build/thor-auto-config-test";Check 'Automatic activation configuration selftest'
& $cc @common "$root/tests/auto_ipc_test.cpp" -o "$build/thor-auto-ipc-test" -ldl;Check 'Automatic activation companion integration selftest'
& $cc @common "$root/tests/auto_provider_test.cpp" -o "$build/thor-auto-provider-test";Check 'Runtime availability selftest'
& $cc @common "$root/tests/frame_worker_test.cpp" -o "$build/thor-frame-worker-test";Check 'Asynchronous frame worker selftest'
& $cc @common "$root/tests/queue_plan_test.cpp" -o "$build/thor-queue-plan-test";Check 'Private queue reservation selftest'
& $cc @common "$root/tests/wsi_guard_test.cpp" -o "$build/thor-wsi-guard-test";Check 'Asynchronous WSI host synchronization selftest'
New-Item -ItemType Directory -Force -Path "$root/module/zygisk" | Out-Null
& $cc @common -shared "$root/native/zygisk.cpp" -o "$root/module/zygisk/arm64-v8a.so" -llog '-Wl,-z,max-page-size=16384';Check 'Zygisk bootstrap'
# Zip entries must use '/', including when built on Windows (Magisk extracts on Linux).
Add-Type -AssemblyName System.IO.Compression
$moduleRoot=[IO.Path]::GetFullPath("$root/module")
$zipFile=[IO.File]::Open("$root/ThorNativeFG-Zygisk-experimental.zip",[IO.FileMode]::Create)
$archive=[IO.Compression.ZipArchive]::new($zipFile,[IO.Compression.ZipArchiveMode]::Create)
try{foreach($file in Get-ChildItem -LiteralPath $moduleRoot -Recurse -File){$relative=$file.FullName.Substring($moduleRoot.Length+1).Replace('\','/');$entry=$archive.CreateEntry($relative);$stream=$entry.Open();$inputFile=[IO.File]::OpenRead($file.FullName);try{$inputFile.CopyTo($stream)}finally{$inputFile.Dispose();$stream.Dispose()}}}finally{$archive.Dispose();$zipFile.Dispose()}
# A controller update must not remove a layer path already bound in a game.
# Install each distinct engine as an immutable APK, with no launcher or service.
$runtimeHash=(Get-FileHash -LiteralPath "$build/lib/arm64-v8a/libVkLayer_ThorFG.so" -Algorithm SHA256).Hash.ToLowerInvariant()
$runtimePackage="com.thor.nativefg.runtime.v$runtimeHash"
New-Item -ItemType Directory -Force -Path "$build/runtime/lib/arm64-v8a","$build/assets","$build/generated/java/com/thor/nativefg" | Out-Null
foreach($assets in @("$build/runtime/assets","$build/assets")){
 New-Item -ItemType Directory -Force -Path "$assets/legal/Anime4K","$assets/legal/SGSR1" | Out-Null
 Copy-Item -LiteralPath "$root/third_party/anime4k/LICENSE.txt","$root/third_party/anime4k/Anime4K_Upscale_CNN_x2_S.glsl","$root/shaders/neural_shuffle.comp" -Destination "$assets/legal/Anime4K" -Force
 Copy-Item -LiteralPath "$root/third_party/Snapdragon-GSR-LICENSE.txt","$root/third_party/sgsr1_shader_mobile.frag" -Destination "$assets/legal/SGSR1" -Force
}
Copy-Item -LiteralPath "$build/lib/arm64-v8a/libVkLayer_ThorFG.so" -Destination "$build/runtime/lib/arm64-v8a/libVkLayer_ThorFG.so" -Force
$runtimeManifest='<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="'+$runtimePackage+'" android:versionCode="1" android:versionName="1"><uses-sdk android:minSdkVersion="29" android:targetSdkVersion="33"/><application android:label="Thor Native FG Runtime" android:hasCode="false" android:allowBackup="false" android:extractNativeLibs="true"/></manifest>'
[IO.File]::WriteAllText("$build/runtime/AndroidManifest.xml",$runtimeManifest,[Text.UTF8Encoding]::new($false))
$key="$build/thor-native-development.jks"
if(-not(Test-Path -LiteralPath $key)){& "$jdk/bin/keytool.exe" -genkeypair -keystore $key -storepass android -keypass android -alias thor -dname 'CN=Thor Native Development' -keyalg RSA -keysize 2048 -validity 3650;Check 'keytool'}
& "$bt/aapt.exe" package -f -M "$build/runtime/AndroidManifest.xml" -A "$build/runtime/assets" -I $androidJar -F "$build/runtime/unsigned.apk";Check 'runtime aapt'
& "$jdk/bin/jar.exe" uf "$build/runtime/unsigned.apk" -C "$build/runtime" lib;Check 'runtime jar'
& "$bt/zipalign.exe" -f -p 4 "$build/runtime/unsigned.apk" "$build/runtime/aligned.apk";Check 'runtime zipalign'
& "$bt/apksigner.bat" sign --ks $key --ks-key-alias thor --ks-pass pass:android --key-pass pass:android --out "$build/assets/thor-runtime.apk" "$build/runtime/aligned.apk";Check 'runtime sign'
& "$bt/apksigner.bat" verify "$build/assets/thor-runtime.apk";Check 'runtime verify'
$runtimeSource='package com.thor.nativefg; final class RuntimeBuild { static final String PACKAGE="'+$runtimePackage+'"; static final String SHA256="'+$runtimeHash+'"; }'
[IO.File]::WriteAllText("$build/generated/java/com/thor/nativefg/RuntimeBuild.java",$runtimeSource,[Text.UTF8Encoding]::new($false))
$controllerManifest=(Get-Content -LiteralPath "$root/AndroidManifest.xml" -Raw).Replace('<queries>','<queries><package android:name="'+$runtimePackage+'"/>')
[IO.File]::WriteAllText("$build/AndroidManifest.xml",$controllerManifest,[Text.UTF8Encoding]::new($false))
$sources=@(Get-ChildItem -LiteralPath "$root/src" -Recurse -Filter '*.java' | ForEach-Object FullName)
$sources+="$build/generated/java/com/thor/nativefg/RuntimeBuild.java"
& "$jdk/bin/javac.exe" -encoding UTF-8 -source 8 -target 8 -bootclasspath "$androidJar;$bt/core-lambda-stubs.jar" -d "$build/classes" @sources;Check 'javac'
$classes=@(Get-ChildItem -LiteralPath "$build/classes" -Recurse -Filter '*.class' | ForEach-Object FullName)
& "$bt/d8.bat" --lib $androidJar --min-api 29 --output "$build/dex" @classes;Check 'd8'
& "$bt/aapt.exe" package -f -M "$build/AndroidManifest.xml" -A "$build/assets" -I $androidJar -F "$build/unsigned.apk";Check 'aapt'
& "$jdk/bin/jar.exe" uf "$build/unsigned.apk" -C "$build/dex" classes.dex -C $build lib;Check 'jar'
& "$bt/zipalign.exe" -f -p 4 "$build/unsigned.apk" "$build/aligned.apk";Check 'zipalign'
& "$bt/apksigner.bat" sign --ks $key --ks-key-alias thor --ks-pass pass:android --key-pass pass:android --out "$root/ThorNativeFG-debug.apk" "$build/aligned.apk";Check 'sign'
& "$bt/apksigner.bat" verify --verbose "$root/ThorNativeFG-debug.apk";Check 'verify'
Get-FileHash -LiteralPath "$root/ThorNativeFG-debug.apk" -Algorithm SHA256
