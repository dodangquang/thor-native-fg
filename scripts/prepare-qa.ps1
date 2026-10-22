$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$bt='E:/DevTools/Android/Sdk/build-tools/35.0.0'
$androidJar='E:/DevTools/Android/Sdk/platforms/android-34/android.jar'
$jdk='C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot'
$qa="$root/build/qa"
New-Item -ItemType Directory -Force -Path "$qa/lib/arm64-v8a","$qa/provider","$qa/probe" | Out-Null
Copy-Item -LiteralPath "$root/research/validation/android-binaries-1.4.363.0/arm64-v8a/libVkLayer_khronos_validation.so" -Destination "$qa/lib/arm64-v8a/libVkLayer_khronos_validation.so"
$manifest='<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.thor.validation" android:versionCode="1"><uses-sdk android:minSdkVersion="29" android:targetSdkVersion="33"/><application android:label="Thor QA Validation Provider" android:hasCode="false" android:extractNativeLibs="true"/></manifest>'
[IO.File]::WriteAllText("$qa/provider/AndroidManifest.xml",$manifest)
& "$bt/aapt.exe" package -f -M "$qa/provider/AndroidManifest.xml" -I $androidJar -F "$qa/provider-unsigned.apk"
if($LASTEXITCODE){throw 'aapt provider'}
& "$jdk/bin/jar.exe" uf "$qa/provider-unsigned.apk" -C $qa lib
& "$bt/zipalign.exe" -f -p 4 "$qa/provider-unsigned.apk" "$qa/provider-aligned.apk"
& "$bt/apksigner.bat" sign --ks "$root/build/thor-native-development.jks" --ks-key-alias thor --ks-pass pass:android --key-pass pass:android --out "$qa/provider.apk" "$qa/provider-aligned.apk"
$probe=(Get-Content -LiteralPath "$root/AndroidManifest.xml" -Raw).Replace('package="com.thor.nativefg"','package="com.thor.nativefg.releaseprobe"').Replace('android:debuggable="true"','android:debuggable="false"').Replace('android:name=".MainActivity"','android:name="com.thor.nativefg.MainActivity"').Replace('android:name=".DemoActivity"','android:name="com.thor.nativefg.DemoActivity"').Replace('android:name=".FpsOverlayService"','android:name="com.thor.nativefg.FpsOverlayService"').Replace('<meta-data android:name="com.android.graphics.injectLayers.enable" android:value="true"/>','')
[IO.File]::WriteAllText("$qa/probe/AndroidManifest.xml",$probe)
& "$bt/aapt.exe" package -f -M "$qa/probe/AndroidManifest.xml" -I $androidJar -F "$qa/probe-unsigned.apk"
& "$jdk/bin/jar.exe" uf "$qa/probe-unsigned.apk" -C "$root/build/dex" classes.dex -C "$root/build" lib
& "$bt/zipalign.exe" -f -p 4 "$qa/probe-unsigned.apk" "$qa/probe-aligned.apk"
& "$bt/apksigner.bat" sign --ks "$root/build/thor-native-development.jks" --ks-key-alias thor --ks-pass pass:android --key-pass pass:android --out "$qa/releaseprobe.apk" "$qa/probe-aligned.apk"
if($LASTEXITCODE){throw 'QA package signing failed'}
