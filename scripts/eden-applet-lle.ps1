param(
 [string]$Serial='c2b670a0',
 [string]$Package='dev.eden.eden_emulator.nightly',
 [int]$Value=1,
 [switch]$DryRun
)
# Sets the controller applet mode per game. Eden rewrites its global config.ini on
# every launch but keeps per-title overrides, so this is the only durable scope.
$ErrorActionPreference='Continue'
$root=Split-Path $PSScriptRoot -Parent
$base="/sdcard/Android/data/$Package/files"
$load="$base/load"
$custom="$base/config/custom"
function Adb([string[]]$a){$r=& adb.exe -s $Serial @a 2>$null;if($LASTEXITCODE){throw "adb failed: $($a -join ' ')"};return ($r -join "`n").Trim()}
$titles=(Adb @('shell','su','-c',"ls $load")) -split "`n" | Where-Object { $_ -match '^[0-9A-Fa-f]{16}$' } | Sort-Object -Unique
$existing=(Adb @('shell','su','-c',"ls $custom 2>/dev/null")) -split "`n" | Where-Object { $_ -match '^([0-9A-Fa-f]{16})\.ini$' } | ForEach-Object { $_.Substring(0,16).ToUpperInvariant() }
$work=Join-Path $root 'research/eden27/applet-work'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$created=0;$patched=0;$skipped=0
foreach($id in $titles){
 $file="$custom/$id.ini"
 if($existing -contains $id.ToUpperInvariant()){
  Adb @('shell','su','-c',"cp $file /sdcard/Download/applet.ini") | Out-Null
  $local=Join-Path $work "$id.ini"
  & adb.exe -s $Serial pull /sdcard/Download/applet.ini $local  2>$null | Out-Null
  $lines=[IO.File]::ReadAllLines($local)
  $out=New-Object System.Collections.Generic.List[string]
  $inApplet=$false;$done=$false
  foreach($line in $lines){
   $trim=$line.Trim()
   if($trim -match '^\[(.+)\]$'){ $inApplet=($Matches[1] -eq 'LibraryApplet'); if($inApplet -and -not $done){ } }
   if($trim -like 'controller_applet_mode*'){ continue }
   if($inApplet -and -not $done -and $trim -eq '[LibraryApplet]'){
    $out.Add($line)
    $out.Add("controller_applet_mode\use_global=false")
    $out.Add("controller_applet_mode\default=false")
    $out.Add("controller_applet_mode=$Value")
    $done=$true
    continue
   }
   $out.Add($line)
  }
  if(-not $done){ $out.Add('[LibraryApplet]');$out.Add("controller_applet_mode\use_global=false");$out.Add("controller_applet_mode\default=false");$out.Add("controller_applet_mode=$Value") }
  $text=($out -join "`n")+"`n"
  [IO.File]::WriteAllText($local,$text)
  if(-not $DryRun){
   & adb.exe -s $Serial push $local /sdcard/Download/applet-new.ini  2>$null | Out-Null
   Adb @('shell','su','-c',"cat /sdcard/Download/applet-new.ini > '$file'") | Out-Null
  }
  $patched++
 }else{
  $text="[LibraryApplet]`ncontroller_applet_mode\use_global=false`ncontroller_applet_mode\default=false`ncontroller_applet_mode=$Value`n"
  $local=Join-Path $work "$id.ini"
  [IO.File]::WriteAllText($local,$text)
  if(-not $DryRun){
   & adb.exe -s $Serial push $local /sdcard/Download/applet-new.ini  2>$null | Out-Null
   Adb @('shell','su','-c',"cat /sdcard/Download/applet-new.ini > '$file'") | Out-Null
  }
  $created++
 }
}
"titles=$($titles.Count) patched=$patched created=$created dryRun=$($DryRun.IsPresent)"
