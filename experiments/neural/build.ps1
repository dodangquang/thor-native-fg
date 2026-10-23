param([string]$Tools='E:/DevTools/ThorML',[string]$Sdk='E:/DevTools/Android/Sdk')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$cc=Join-Path $Sdk 'ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android29-clang++.cmd'
$out=Join-Path $root 'work/neural'
New-Item -ItemType Directory -Force -Path $out | Out-Null
& $cc -std=c++17 -O2 -static-libstdc++ "$PSScriptRoot/nnapi_probe.cpp" -lneuralnetworks -o "$out/thor-nnapi-probe"
if($LASTEXITCODE){throw 'NNAPI capability probe build failed'}
& $cc -std=c++17 -O2 -static-libstdc++ -I "$Tools/onnx-include" "$PSScriptRoot/onnx_probe.cpp" -L "$Tools/android" -lonnxruntime -o "$out/thor-onnx-probe"
if($LASTEXITCODE){throw 'ONNX QNN probe build failed'}
$shaderBin=Join-Path $Sdk 'ndk/27.3.13750724/shader-tools/windows-x86_64'
$header="#pragma once`n#include <cstdint>`n"
foreach($name in 'conv','conv_naive','shuffle','anime0','anime1','anime2','anime3','anime4'){
 $shader=if($name.StartsWith('anime')){"$root/research/anime4k/generated/$name.comp"}else{"$PSScriptRoot/$name.comp"}
 & "$shaderBin/glslc.exe" --target-env=vulkan1.1 -O $shader -o "$out/$name.spv"
 if($LASTEXITCODE){throw "Neural shader $name failed"}
 & "$shaderBin/spirv-val.exe" --target-env vulkan1.1 "$out/$name.spv"
 if($LASTEXITCODE){throw "Neural SPIR-V $name invalid"}
 $bytes=[IO.File]::ReadAllBytes("$out/$name.spv")
 $words=for($i=0;$i -lt $bytes.Length;$i+=4){'0x'+[BitConverter]::ToUInt32($bytes,$i).ToString('x8')+'u'}
 $header+='static const uint32_t neural_'+$name+'_spv[]={' + ($words -join ',') + "};`n"
}
[IO.File]::WriteAllText("$out/neural_shaders.hpp",$header,[Text.UTF8Encoding]::new($false))
& $cc -std=c++17 -O2 -static-libstdc++ -I "$out" -I "$root/native" -I "$root/build/generated" "$root/native/engine.cpp" "$PSScriptRoot/vulkan_probe.cpp" -lvulkan -llog -o "$out/thor-neural-vulkan"
if($LASTEXITCODE){throw 'Direct Vulkan neural probe build failed'}
