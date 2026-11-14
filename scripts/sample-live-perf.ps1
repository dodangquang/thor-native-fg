param([string]$Serial='c2b670a0',[int]$Samples=40,[int]$IntervalSec=3,[string]$OutFile='research/droiddeck/live-perf.csv')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not [IO.Path]::IsPathRooted($OutFile)){$OutFile=Join-Path $root $OutFile}
function Adb([string[]]$a){return ((& adb.exe -s $Serial @a) -join "`n").Trim()}
$rows=@()
for($i=1;$i -le $Samples;$i++){
 $raw=Adb @('shell','cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage; cat /sys/class/kgsl/kgsl-3d0/clock_mhz; cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq; cat /sys/devices/system/cpu/cpu3/cpufreq/scaling_cur_freq; cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq')
 $v=($raw -split "`n") | ForEach-Object { ($_ -replace '[^0-9]','') }
 $metrics=Adb @('logcat','-d','-s','ThorNativeLayer:I')
 $line=($metrics -split "`n" | Where-Object {$_ -match 'THOR_METRICS'} | Select-Object -Last 1)
 $src=0;$gen=0;$disp=0;$gapmax=0
 if($line){
  if($line -match 'source_fps=([0-9.]+)'){$src=[double]$Matches[1]}
  if($line -match 'generated_fps=([0-9.]+)'){$gen=[double]$Matches[1]}
  if($line -match 'display_valid=1 display_fps=([0-9.]+)'){$disp=[double]$Matches[1]}
  if($line -match 'gap_max_ms=([0-9.]+)'){$gapmax=[double]$Matches[1]}
 }
 $game=Adb @('shell','pgrep','-f','Dungeons-Win64-Shipping')
 $cpums=0;$threads=0;$aff='';$gamePid=''
 if($game -match '\d+'){
  $gamePid=($game -split "`n")[0].Trim()
  $cpums=[int](Adb @('shell','su','-c',"awk '{print `$14+`$15}' /proc/$gamePid/stat"))
  $threads=(Adb @('shell','ls',"/proc/$gamePid/task")).Split("`n").Count
  $aff=Adb @('shell','su','-c',"taskset -p $gamePid")
 }
 $rows+=[pscustomobject]@{t=$i;gpu_busy=[int]$v[0];gpu_mhz=[int]$v[1];cpu0=[int]$v[2];cpu3=[int]$v[3];cpu7=[int]$v[4];source_fps=$src;generated_fps=$gen;display_fps=$disp;gap_max_ms=$gapmax;game_cpu_ticks=$cpums;game_threads=$threads;affinity=$aff;game_pid=$gamePid}
 Start-Sleep -Seconds $IntervalSec
}
$rows | Export-Csv -LiteralPath $OutFile -NoTypeInformation -Encoding UTF8
$srcSorted=($rows.source_fps | Sort-Object);$dispSorted=($rows.display_fps | Sort-Object)
"rows=$($rows.Count) game_pid=$($rows[-1].game_pid) src_median=$($srcSorted[[int]($rows.Count/2)]) disp_median=$($dispSorted[[int]($rows.Count/2)]) affinity=$($rows[-1].affinity) threads=$($rows[-1].game_threads)"
