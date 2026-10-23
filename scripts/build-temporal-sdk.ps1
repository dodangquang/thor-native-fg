param([string]$Sdk='E:/DevTools/Android/Sdk')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$dest="$root/build/temporal-sdk"
$bin="$Sdk/ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/bin"
New-Item -ItemType Directory -Force -Path "$dest/include" | Out-Null
& "$PSScriptRoot/prepare-temporal-shaders.ps1" -Destination "$root/build/generated" -Sdk $Sdk
foreach($source in @('engine','temporal')){
 & "$bin/aarch64-linux-android29-clang++.cmd" -std=c++17 -O2 -fPIC -ffunction-sections -fdata-sections -I "$root/native" -I "$root/build/generated" -c "$root/native/$source.cpp" -o "$dest/$source.o"
 if($LASTEXITCODE){throw "Temporal SDK compile $source failed"}
}
& "$bin/llvm-ar.exe" rcs "$dest/libthor_temporal.a" "$dest/engine.o" "$dest/temporal.o"
if($LASTEXITCODE){throw 'Temporal SDK archive failed'}
foreach($name in @('temporal.hpp','temporal_sequence.hpp','engine.hpp','quality.hpp','vkapi.hpp')){Copy-Item -LiteralPath "$root/native/$name" -Destination "$dest/include/$name" -Force}
New-Item -ItemType Directory -Force -Path "$dest/legal/SGSR2","$dest/legal/SGSR1","$dest/legal/Anime4K" | Out-Null
Copy-Item -LiteralPath "$root/third_party/sgsr2/LICENSE","$root/third_party/sgsr2/PINNED.txt","$root/third_party/sgsr2/sgsr2_convert.fs","$root/third_party/sgsr2/sgsr2_upscale.fs","$root/scripts/prepare-temporal-shaders.py" -Destination "$dest/legal/SGSR2" -Force
Copy-Item -LiteralPath "$root/third_party/Snapdragon-GSR-LICENSE.txt","$root/third_party/sgsr1_shader_mobile.frag" -Destination "$dest/legal/SGSR1" -Force
Copy-Item -LiteralPath "$root/third_party/anime4k/LICENSE.txt","$root/third_party/anime4k/Anime4K_Upscale_CNN_x2_S.glsl" -Destination "$dest/legal/Anime4K" -Force
$files=@('native/engine.cpp','native/temporal.cpp','native/temporal.hpp','native/temporal_sequence.hpp','native/vkapi.hpp','build/generated/shaders.hpp','build/generated/temporal_shaders.hpp')
$hashes=@{};foreach($file in $files){$hashes[$file]=(Get-FileHash -LiteralPath "$root/$file" -Algorithm SHA256).Hash.ToLowerInvariant()}
$provenance=@{target='arm64-v8a Android29 C++17 NDK27.3.13750724';archive_sha256=(Get-FileHash -LiteralPath "$dest/libthor_temporal.a" -Algorithm SHA256).Hash.ToLowerInvariant();source_hashes=$hashes;status='cooperative renderer backend, not an emulator provider or installable game activation'}
[IO.File]::WriteAllText("$dest/provenance.json",($provenance|ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
Write-Output "TEMPORAL_SDK archive=$dest/libthor_temporal.a sha256=$($provenance.archive_sha256)"
