param([Parameter(Mandatory=$true)][string]$Fixtures,[Parameter(Mandatory=$true)][string]$Results,[string]$ShaderDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$cc='E:/DevTools/Android/Sdk/ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android29-clang++.cmd'
$adb='E:/DevTools/Android/Sdk/platform-tools/adb.exe'
$exe="$root/build/thor-frame-quality"
if(-not $ShaderDirectory){$ShaderDirectory="$root/build/generated"}
& $cc -std=c++17 -O2 -fPIC -static-libstdc++ -I "$root/native" -I $ShaderDirectory "$root/native/engine.cpp" "$PSScriptRoot/lab.cpp" -lvulkan -llog -o $exe
if($LASTEXITCODE){throw 'FG quality lab compile failed'}
$device='/data/local/tmp/thor-frame-quality-lab'
& $adb -s c2b670a0 shell mkdir -p $device "$device/results"
& $adb -s c2b670a0 push $exe "$device/lab"
& $adb -s c2b670a0 push "$Fixtures/pairs.bin" "$device/pairs.bin"
& $adb -s c2b670a0 shell chmod 755 "$device/lab"
New-Item -ItemType Directory -Force -Path $Results | Out-Null
& $adb -s c2b670a0 shell "$device/lab" "$device/pairs.bin" "$device/results" | Tee-Object -FilePath "$Results/execution.txt"
if($LASTEXITCODE){throw 'FG quality GPU run failed'}
& $adb -s c2b670a0 pull "$device/results/." $Results
if($LASTEXITCODE){throw 'FG quality collection failed'}
Get-FileHash "$ShaderDirectory/shaders.hpp",$exe -Algorithm SHA256 | Format-List | Out-File "$Results/provenance.txt"
