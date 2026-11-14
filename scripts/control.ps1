param(
 [ValidateSet('Enable','Disable','Status')][string]$Action='Status',
 [ValidatePattern('^[A-Za-z0-9_.]+$')][string]$Package,
 [switch]$NoUpscale,
 [string]$Serial='c2b670a0'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$backup=Join-Path $root 'research/adb-controller-before.json'
$keys=@('enable_gpu_debug_layers','gpu_debug_app','gpu_debug_layers','gpu_debug_layer_app')
function Adb([string[]]$Arguments){$result=& adb.exe -s $Serial @Arguments;if($LASTEXITCODE){throw "adb failed ($LASTEXITCODE): $($Arguments -join ' ')"};return ($result -join "`n").Trim()}
if($Action -eq 'Enable'){
 if(-not $Package){throw 'Specify -Package with an installed Android package name.'}
 if($Package.Length -gt 90){throw 'Package name exceeds Android debug property limit.'}
 $null=Adb @('shell','pm','path',$Package)
 if(-not(Test-Path -LiteralPath $backup)){
  $previous=[ordered]@{};foreach($key in $keys){$previous[$key]=Adb @('shell','settings','get','global',$key)}
  $previous.thor_enabled=Adb @('shell','getprop','debug.thorfg.enabled');$previous.thor_target=Adb @('shell','getprop','debug.thorfg.target')
  $previous.thor_sr=Adb @('shell','getprop','debug.thorfg.sr')
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
  $previous | ConvertTo-Json | Set-Content -LiteralPath $backup
 }
 $null=Adb @('shell','settings','put','global','enable_gpu_debug_layers','1')
 $null=Adb @('shell','settings','put','global','gpu_debug_app',$Package)
 $null=Adb @('shell','settings','put','global','gpu_debug_layers','VK_LAYER_THOR_native_fg')
 $null=Adb @('shell','settings','put','global','gpu_debug_layer_app','com.thor.nativefg')
 $null=Adb @('shell','setprop','debug.thorfg.target',$Package)
 $null=Adb @('shell','setprop','debug.thorfg.enabled','1')
 $null=Adb @('shell','setprop','debug.thorfg.sr',$(if($NoUpscale){'0'}else{'1'}))
 Write-Output "Configured $Package. Save your game, fully exit and reopen the emulator, then select Vulkan. Use Status to verify actual loading."
}elseif($Action -eq 'Disable'){
 $null=Adb @('shell','setprop','debug.thorfg.enabled','0')
 $null=Adb @('shell','setprop','debug.thorfg.sr','0')
 $null=Adb @('shell',"setprop debug.thorfg.target ''")
 if(Test-Path -LiteralPath $backup){
  $previous=Get-Content -LiteralPath $backup -Raw | ConvertFrom-Json
  foreach($key in $keys){$value=$previous.$key;if(-not $value -or $value -eq 'null'){$null=Adb @('shell','settings','delete','global',$key)}else{$null=Adb @('shell','settings','put','global',$key,$value)}}
  if($previous.thor_target -match '^[A-Za-z0-9_.]+$'){$null=Adb @('shell','setprop','debug.thorfg.target',$previous.thor_target)}
  if($previous.thor_enabled -eq '1'){$null=Adb @('shell','setprop','debug.thorfg.enabled','1')}
  if($previous.thor_sr -eq '1'){$null=Adb @('shell','setprop','debug.thorfg.sr','1')}
  Remove-Item -LiteralPath $backup
 }
 Write-Output 'Disabled and restored the saved settings. Restart the emulator to unload its layer.'
}else{
 $target=Adb @('shell','getprop','debug.thorfg.target');$active=Adb @('shell','getprop','debug.thorfg.enabled');Write-Output "Target=$target Active=$active"
 if($target -match '^[A-Za-z0-9_.]+$'){
  $process=Adb @('shell',"pidof -s $target || true")
  if($process -match '^\d+$'){Write-Output "PID=$process";Adb @('logcat','-d',"--pid=$process",'-t','2500','-s','ThorNativeBootstrap:I','ThorNativeLayer:I','*:S')}
 }
}
