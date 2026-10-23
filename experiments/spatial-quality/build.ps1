param([string]$DataDirectory='E:/ThorNative/research/spatial-quality-tiny')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$sdk='E:/DevTools/Android/Sdk'
$bt="$sdk/build-tools/35.0.0"
$jdk='C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot'
$cc="$sdk/ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android29-clang++.cmd"
$lab="$root/build/spatial-quality"
function Check([string]$stage){if($LASTEXITCODE){throw "$stage failed ($LASTEXITCODE)"}}
$fixture=(Resolve-Path -LiteralPath "$DataDirectory/fixtures.bin").Path
$fixtureHash=(Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash.ToLowerInvariant()
New-Item -ItemType Directory -Force -Path "$lab/lib/arm64-v8a","$lab/assets/legal/Anime4K","$lab/assets/legal/SGSR1","$lab/classes","$lab/dex" | Out-Null
& $cc -std=c++17 -O2 -static-libstdc++ -I "$root/native" -I "$root/build/generated" "$root/native/engine.cpp" "$PSScriptRoot/lab.cpp" -lvulkan -llog -o "$root/build/thor-spatial-lab";Check 'Spatial lab CLI'
& $cc -std=c++17 -O2 -fPIC -shared -static-libstdc++ -DTHOR_SPATIAL_LAB_JNI -I "$root/native" -I "$root/build/generated" "$root/native/engine.cpp" "$PSScriptRoot/lab.cpp" -lvulkan -llog '-Wl,-z,max-page-size=16384' -o "$lab/lib/arm64-v8a/libthor_spatial_lab.so";Check 'Spatial lab JNI'
$nativeHash=(Get-FileHash -LiteralPath "$lab/lib/arm64-v8a/libthor_spatial_lab.so" -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceHashes=@{};foreach($path in @('native/engine.cpp','native/engine.hpp','build/generated/shaders.hpp','shaders/neural_bounds.glsl','shaders/neural_shuffle.comp','shaders/neural_resize.comp','experiments/spatial-quality/lab.cpp')){$sourceHashes[$path]=(Get-FileHash -LiteralPath "$root/$path" -Algorithm SHA256).Hash.ToLowerInvariant()}
$provenance=@{fixtures_sha256=$fixtureHash;native_sha256=$nativeHash;source_hashes=$sourceHashes}
[IO.File]::WriteAllText("$lab/assets/provenance.json",($provenance | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath "$root/research/validation/android-binaries-1.4.363.0/arm64-v8a/libVkLayer_khronos_validation.so" -Destination "$lab/lib/arm64-v8a/libVkLayer_khronos_validation.so" -Force
Copy-Item -LiteralPath "$sdk/ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" -Destination "$lab/lib/arm64-v8a/libc++_shared.so" -Force
Copy-Item -LiteralPath $fixture -Destination "$lab/assets/fixtures.bin" -Force
Copy-Item -LiteralPath "$DataDirectory/manifest.json" -Destination "$lab/assets/manifest.json" -Force
Copy-Item -LiteralPath "$root/third_party/anime4k/LICENSE.txt","$root/third_party/anime4k/Anime4K_Upscale_CNN_x2_S.glsl" -Destination "$lab/assets/legal/Anime4K" -Force
Copy-Item -LiteralPath "$root/third_party/Snapdragon-GSR-LICENSE.txt","$root/third_party/sgsr1_shader_mobile.frag" -Destination "$lab/assets/legal/SGSR1" -Force
$manifest='<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.thor.nativefg.spatialqa" android:versionCode="1"><uses-sdk android:minSdkVersion="29" android:targetSdkVersion="33"/><application android:debuggable="true" android:extractNativeLibs="true" android:allowBackup="false" android:label="Thor Spatial Quality Lab"/><instrumentation android:name="com.thor.spatialqa.SpatialInstrumentation" android:targetPackage="com.thor.nativefg.spatialqa"/></manifest>'
[IO.File]::WriteAllText("$lab/AndroidManifest.xml",$manifest,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText("$lab/LabFixture.java",'package com.thor.spatialqa; final class LabFixture { static final String SHA256="'+$fixtureHash+'"; static final String NATIVE_SHA256="'+$nativeHash+'"; }',[Text.UTF8Encoding]::new($false))
& "$jdk/bin/javac.exe" -encoding UTF-8 -source 8 -target 8 -bootclasspath "$sdk/platforms/android-34/android.jar;$bt/core-lambda-stubs.jar" -d "$lab/classes" "$PSScriptRoot/SpatialInstrumentation.java" "$lab/LabFixture.java";Check 'Spatial instrumentation Java'
$classes=@(Get-ChildItem -LiteralPath "$lab/classes" -Recurse -Filter '*.class' | ForEach-Object FullName)
& "$bt/d8.bat" --lib "$sdk/platforms/android-34/android.jar" --min-api 29 --output "$lab/dex" @classes;Check 'Spatial instrumentation dex'
& "$bt/aapt.exe" package -f -M "$lab/AndroidManifest.xml" -A "$lab/assets" -I "$sdk/platforms/android-34/android.jar" -F "$lab/unsigned.apk";Check 'Spatial APK assets'
& "$jdk/bin/jar.exe" uf "$lab/unsigned.apk" -C $lab lib -C "$lab/dex" classes.dex;Check 'Spatial APK code'
& "$bt/zipalign.exe" -f -p 4 "$lab/unsigned.apk" "$lab/aligned.apk";Check 'Spatial APK alignment'
& "$bt/apksigner.bat" sign --ks "$root/build/thor-native-development.jks" --ks-key-alias thor --ks-pass pass:android --key-pass pass:android --out "$lab/spatial-quality-qa.apk" "$lab/aligned.apk";Check 'Spatial APK signing'
& "$bt/apksigner.bat" verify "$lab/spatial-quality-qa.apk";Check 'Spatial APK signature'
Write-Output "SPATIAL_LAB_BUILD fixtures_sha256=$fixtureHash apk=$lab/spatial-quality-qa.apk"
