param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$upstream=Join-Path $root 'third_party/anime4k/Anime4K_Upscale_CNN_x2_S.glsl'
if((Get-FileHash -LiteralPath $upstream -Algorithm SHA256).Hash -ne '4C53EC2E287908F7EE7BCB266B0170421626D663576468B7D7DAFC62962649A4'){throw 'Neural model source SHA256 mismatch'}
$source=Get-Content -LiteralPath $upstream -Raw
$parts=[regex]::Split($source,'(?m)^//!DESC ')
if($parts.Length -ne 6){throw 'Unexpected neural shader pass count'}
$notice=$parts[0]
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$prefix=@'
#version 450
layout(local_size_x=8,local_size_y=8) in;
layout(binding=0) uniform sampler2D inputImage;
layout(binding=1,rgba16f) uniform writeonly image2D outputImage;
layout(push_constant) uniform Params {ivec2 size;} p;
'@
for($index=0;$index -lt 4;$index++){
 $section=$parts[$index+1]
 $binding=[regex]::Match($section,'(?m)^//!BIND (\w+)').Groups[1].Value
 if(!$binding){throw 'Neural input binding missing'}
 $body=$section.Substring($section.IndexOf('#define')).Replace('vec4 hook()','vec4 infer()')
 $body=[regex]::Replace($body,'(?m)^//!.*\r?\n','')
 $sample="#define ${binding}_texOff(offset) texelFetch(inputImage,clamp(ivec2(gl_GlobalInvocationID.xy)+ivec2(offset),ivec2(0),p.size-1),0)`n"
 $entry="`nvoid main(){ivec2 at=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(at,p.size)))return;imageStore(outputImage,at,infer());}`n"
 [IO.File]::WriteAllText("$Destination/neural$index.comp",$prefix+"`n"+$notice+$sample+$body+$entry,[Text.UTF8Encoding]::new($false))
}
