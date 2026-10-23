param([string]$Tools='E:/DevTools/ThorML',[string]$Sdk='E:/DevTools/Android/Sdk')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$research=Join-Path $root 'research'
$python='C:/Users/dodan/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
New-Item -ItemType Directory -Force -Path "$research/quicksrnet","$research/anime4k/generated","$Tools/android","$Tools/onnx-include","$Tools/licenses" | Out-Null
function Fetch([string]$url,[string]$name,[string]$sha){
 $path=Join-Path "$research/quicksrnet" $name
 if(-not(Test-Path -LiteralPath $path)){Invoke-WebRequest -Uri $url -OutFile $path}
 if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $sha){throw "SHA256 mismatch: $path"}
 return $path
}
$checkpoint=Fetch 'https://github.com/quic/aimet-model-zoo/releases/download/phase_2_january_artifacts/quicksrnet_small_2x_checkpoint_float32.pth.tar' 'quicksrnet_small_2x_checkpoint_float32.pth.tar' 'D95D70F1D2366CB9C28D99F8C7AA5BB07E1FFEAF5D7E30D9C66AB0FA28C6D0F8'
$ort=Fetch 'https://repo.maven.apache.org/maven2/com/microsoft/onnxruntime/onnxruntime-android-qnn/1.22.0/onnxruntime-android-qnn-1.22.0.aar' 'onnxruntime-android-qnn-1.22.0.aar' '8BE5CC2E5A56BF44673582661CA0E3731C12A9D57FAE789C92F367E67755275F'
$qnn=Fetch 'https://repo.maven.apache.org/maven2/com/qualcomm/qti/qnn-runtime/2.33.0/qnn-runtime-2.33.0.aar' 'qnn-runtime-2.33.0.aar' 'D41CD9BAD577734C1EE165EC5977EF239E4AAF72DDA25CACB137E70D37737BFC'
foreach($package in @(@($ort,'onnx'),@($qnn,'qnn'))){
 $zip=[IO.Compression.ZipFile]::OpenRead($package[0])
 try{
  foreach($entry in $zip.Entries){
   $name=Split-Path $entry.FullName -Leaf
   if(!$name){continue}
   $target=$null
   if($entry.FullName -like 'headers/*'){$target=Join-Path "$Tools/onnx-include" $name}
   if($entry.FullName -match '^jni/arm64-v8a/(libonnxruntime.so|libQnnHtp.so|libQnnHtpPrepare.so|libQnnHtpV73Stub.so|libQnnHtpV73Skel.so|libQnnSystem.so|libQnnGpu.so)$'){$target=Join-Path "$Tools/android" $name}
   if($entry.FullName -match '^(NOTICE.txt|LICENSE.pdf)$'){$target=Join-Path "$Tools/licenses" ($package[1]+'-'+$name)}
   if($target){[IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$true)}
  }
 }finally{$zip.Dispose()}
}
Copy-Item -LiteralPath "$Sdk/ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" -Destination "$Tools/android/libc++_shared.so"
if(-not(Test-Path -LiteralPath "$Tools/python/onnxruntime")){
 & $python -m pip install --disable-pip-version-check --target "$Tools/python" onnx==1.17.0 onnxruntime==1.22.0
 if($LASTEXITCODE){throw 'Experiment Python dependencies failed'}
}
$previousPythonPath=$env:PYTHONPATH
try{
 $env:PYTHONPATH="$Tools/python"
 & $python "$PSScriptRoot/export_quicksrnet.py" $checkpoint "$research/quicksrnet/models" --width 32 --height 32
 if($LASTEXITCODE){throw 'QuickSRNet export/reference failed'}
 & $python "$PSScriptRoot/prepare_anime4k.py" "$root/third_party/anime4k/Anime4K_Upscale_CNN_x2_S.glsl" "$research/anime4k/generated"
 if($LASTEXITCODE){throw 'Anime4K conversion/reference failed'}
}finally{$env:PYTHONPATH=$previousPythonPath}
