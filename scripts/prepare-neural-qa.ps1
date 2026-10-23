$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$sdk='E:/DevTools/Android/Sdk'
$bt="$sdk/build-tools/35.0.0"
$jdk='C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot'
$cc="$sdk/ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android29-clang++.cmd"
$python='C:/Users/dodan/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$qa="$root/build/neural-qa"
New-Item -ItemType Directory -Force -Path "$qa/lib/arm64-v8a","$qa/assets/legal","$qa/classes","$qa/dex" | Out-Null
& $python "$root/tests/prepare_neural_fixtures.py" "$qa/assets/fixtures.bin"
if($LASTEXITCODE){throw 'Independent neural fixtures failed'}
& $cc -std=c++17 -O2 -fPIC -shared -static-libstdc++ -DTHOR_NEURAL_JNI -I "$root/native" -I "$root/build/generated" "$root/native/engine.cpp" "$root/native/neural_selftest.cpp" -lvulkan -llog '-Wl,-z,max-page-size=16384' -o "$qa/lib/arm64-v8a/libthor_neural_qa.so"
if($LASTEXITCODE){throw 'Neural QA JNI build failed'}
Copy-Item -LiteralPath "$root/research/validation/android-binaries-1.4.363.0/arm64-v8a/libVkLayer_khronos_validation.so" -Destination "$qa/lib/arm64-v8a/libVkLayer_khronos_validation.so"
Copy-Item -LiteralPath "$sdk/ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" -Destination "$qa/lib/arm64-v8a/libc++_shared.so"
Copy-Item -LiteralPath "$root/third_party/anime4k/LICENSE.txt","$root/third_party/anime4k/Anime4K_Upscale_CNN_x2_S.glsl" -Destination "$qa/assets/legal" -Force
$manifest='<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.thor.nativefg.neuralqa" android:versionCode="1"><uses-sdk android:minSdkVersion="29" android:targetSdkVersion="33"/><application android:debuggable="true" android:extractNativeLibs="true" android:allowBackup="false" android:label="Thor Neural Shader QA"/><instrumentation android:name="com.thor.neuralqa.HeadlessInstrumentation" android:targetPackage="com.thor.nativefg.neuralqa"/></manifest>'
[IO.File]::WriteAllText("$qa/AndroidManifest.xml",$manifest,[Text.UTF8Encoding]::new($false))
& "$jdk/bin/javac.exe" -encoding UTF-8 -source 8 -target 8 -bootclasspath "$sdk/platforms/android-34/android.jar;$bt/core-lambda-stubs.jar" -d "$qa/classes" "$root/tests/android/HeadlessInstrumentation.java"
if($LASTEXITCODE){throw 'Neural instrumentation Java build failed'}
$classes=@(Get-ChildItem -LiteralPath "$qa/classes" -Recurse -Filter '*.class' | ForEach-Object FullName)
& "$bt/d8.bat" --lib "$sdk/platforms/android-34/android.jar" --min-api 29 --output "$qa/dex" @classes
if($LASTEXITCODE){throw 'Neural QA dex failed'}
& "$bt/aapt.exe" package -f -M "$qa/AndroidManifest.xml" -A "$qa/assets" -I "$sdk/platforms/android-34/android.jar" -F "$qa/unsigned.apk"
if($LASTEXITCODE){throw 'Neural QA APK packaging failed'}
& "$jdk/bin/jar.exe" uf "$qa/unsigned.apk" -C $qa lib -C "$qa/dex" classes.dex
if($LASTEXITCODE){throw 'Neural QA APK libraries failed'}
& "$bt/zipalign.exe" -f -p 4 "$qa/unsigned.apk" "$qa/aligned.apk"
if($LASTEXITCODE){throw 'Neural QA alignment failed'}
& "$bt/apksigner.bat" sign --ks "$root/build/thor-native-development.jks" --ks-key-alias thor --ks-pass pass:android --key-pass pass:android --out "$qa/neural-qa.apk" "$qa/aligned.apk"
if($LASTEXITCODE){throw 'Neural QA signing failed'}
& "$bt/apksigner.bat" verify "$qa/neural-qa.apk"
if($LASTEXITCODE){throw 'Neural QA signature invalid'}
