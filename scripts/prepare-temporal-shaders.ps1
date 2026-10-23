param([string]$Destination='E:/ThorNative/build/generated',[string]$Sdk='E:/DevTools/Android/Sdk')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$python='C:/Users/dodan/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$bin="$Sdk/ndk/27.3.13750724/shader-tools/windows-x86_64"
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
& $python "$PSScriptRoot/prepare-temporal-shaders.py" "$root/third_party/sgsr2" $Destination --stable --prefix temporal_
if($LASTEXITCODE){throw 'Temporal shader adaptation failed'}
$header="#pragma once`n#include <cstdint>`n"
foreach($name in @('temporal_convert','temporal_upscale')){
 & "$bin/glslc.exe" --target-env=vulkan1.1 -O "$Destination/$name.comp" -o "$Destination/$name.spv"
 if($LASTEXITCODE){throw "Temporal compile $name failed"}
 & "$bin/spirv-val.exe" --target-env vulkan1.1 "$Destination/$name.spv"
 if($LASTEXITCODE){throw "Temporal validation $name failed"}
 $bytes=[IO.File]::ReadAllBytes("$Destination/$name.spv")
 $words=for($i=0;$i -lt $bytes.Length;$i+=4){'0x'+[BitConverter]::ToUInt32($bytes,$i).ToString('x8')+'u'}
 $header+='static const uint32_t '+$name+'_spv[]={' + ($words -join ',') + "};`n"
}
[IO.File]::WriteAllText("$Destination/temporal_shaders.hpp",$header,[Text.UTF8Encoding]::new($false))
