param([string]$Serial='c2b670a0',[string]$Sdk='E:/DevTools/Android/Sdk')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$destination=Join-Path $root 'build/composite-layout-test'
$compiler=Join-Path $Sdk 'ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android29-clang++.cmd'
$adb=Join-Path $Sdk 'platform-tools/adb.exe'
function Check([string]$Step){if($LASTEXITCODE -ne 0){throw "$Step failed: $LASTEXITCODE"}}
New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
& $compiler -std=c++17 -O2 -static-libstdc++ -I "$root/native" "$PSScriptRoot/composite_layout_test.cpp" -o $destination
Check 'compile geometry checks'
& $adb -s $Serial push $destination /data/local/tmp/thor-composite-layout-test
Check 'push geometry checks'
try {
    & $adb -s $Serial shell chmod 755 /data/local/tmp/thor-composite-layout-test
    Check 'allow geometry checks to execute'
    & $adb -s $Serial shell /data/local/tmp/thor-composite-layout-test
    Check 'device geometry checks'
} finally {
    & $adb -s $Serial shell rm -f /data/local/tmp/thor-composite-layout-test
}
