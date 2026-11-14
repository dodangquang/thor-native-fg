param(
 [string]$Serial='c2b670a0',
 [string]$Package='com.thor.nativefg',
 [int]$Samples=20,
 [int]$IntervalSec=5,
 [int]$SettleSec=20,
 [string]$OutDir='research/heat'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not [IO.Path]::IsPathRooted($OutDir)){$OutDir=Join-Path $root $OutDir}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
function Adb([string[]]$a){$r=& adb.exe -s $Serial @a;if($LASTEXITCODE){throw "adb failed: $($a -join ' ')"};return ($r -join "`n").Trim()}
function SetProp([string]$k,[string]$v){$null=Adb @('shell','setprop',$k,$v)}
# Thermal zones are read by name because the numbering differs between builds.
$zones=@{'ddr'='thermal_zone55';'gpuss0'='thermal_zone63';'batt'='thermal_zone94'}
function Sample([string]$state,[int]$index){
 $raw=Adb @('shell','cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage; cat /sys/class/kgsl/kgsl-3d0/clock_mhz; cat /sys/class/thermal/thermal_zone55/temp; cat /sys/class/thermal/thermal_zone63/temp; cat /sys/class/power_supply/battery/temp; cat /sys/class/power_supply/battery/current_now; cat /sys/class/power_supply/battery/voltage_now')
 $v=$raw -split "`n" | ForEach-Object { ($_ -replace '[^0-9\-]','') }
 $busy=[double]$v[0];$mhz=[double]$v[1];$ddr=[double]$v[2]/1000;$gpu=[double]$v[3]/1000;$bt=[double]$v[4]/10
 $ua=[double]$v[5];$uv=[double]$v[6];$watt=[Math]::Abs($ua)/1e6*($uv/1e6)
 [pscustomobject]@{state=$state;sample=$index;gpu_busy_pct=$busy;gpu_mhz=$mhz;ddr_c=$ddr;gpuss0_c=$gpu;battery_c=$bt;current_ua=$ua;voltage_uv=$uv;watts=[Math]::Round($watt,2)}
}
$null=Adb @('shell','am','force-stop',$Package)
SetProp 'debug.thorfg.auto' '0'
SetProp 'debug.thorfg.target' $Package
SetProp 'debug.thorfg.sr' '0'
$null=Adb @('shell',"su -c 'am start -n $Package/.DemoActivity --ei mode 3'")
Start-Sleep -Seconds 10
$rows=@()
$states=@(@('fg_off','0','0'),@('2x','1','0'),@('3x','1','1'))
foreach($s in $states){
 SetProp 'debug.thorfg.enabled' $s[1]
 SetProp 'debug.thorfg.triple' $s[2]
 Start-Sleep -Seconds $SettleSec
 for($i=1;$i -le $Samples;$i++){$rows+=Sample $s[0] $i;Start-Sleep -Seconds $IntervalSec}
 $m=Adb @('logcat','-d','-s','ThorNativeLayer:I')
 $line=($m -split "`n" | Where-Object {$_ -match 'THOR_METRICS'} | Select-Object -Last 1)
 if($line){ ($line -split 'ThorNativeLayer: ')[1] | Set-Content -LiteralPath (Join-Path $OutDir ("metrics-"+$s[0]+".txt")) }
}
$csv=Join-Path $OutDir 'heat.csv'
$rows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8
$rows | Group-Object state | ForEach-Object {
 $g=$_.Group
 [pscustomobject]@{
  state=$_.Name;n=$g.Count
  gpu_busy_pct=[Math]::Round(($g.gpu_busy_pct | Sort-Object)[[int]($g.Count/2)],1)
  gpu_mhz=[Math]::Round(($g.gpu_mhz | Sort-Object)[[int]($g.Count/2)],0)
  ddr_c=[Math]::Round(($g.ddr_c | Sort-Object)[[int]($g.Count/2)],1)
  gpuss0_c=[Math]::Round(($g.gpuss0_c | Sort-Object)[[int]($g.Count/2)],1)
  battery_c=[Math]::Round(($g.battery_c | Sort-Object)[[int]($g.Count/2)],1)
  watts=[Math]::Round(($g.watts | Sort-Object)[[int]($g.Count/2)],2)
 }
} | Format-Table -AutoSize | Out-String -Width 200
