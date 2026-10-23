#!/system/bin/sh
echo "=== CPU CORES ==="
for i in /sys/devices/system/cpu/cpu[0-9]*; do
  cname=$(basename "$i")
  min=$(cat "$i/cpufreq/scaling_min_freq" 2>/dev/null)
  cur=$(cat "$i/cpufreq/scaling_cur_freq" 2>/dev/null)
  max=$(cat "$i/cpufreq/scaling_max_freq" 2>/dev/null)
  gov=$(cat "$i/cpufreq/scaling_governor" 2>/dev/null)
  echo "$cname: cur=$cur min=$min max=$max gov=$gov"
done

echo ""
echo "=== GPU ==="
echo -n "GPU cur freq: "; cat /sys/class/kgsl/kgsl-3d0/devfreq/cur_freq 2>/dev/null
echo -n "GPU avail freqs: "; cat /sys/class/kgsl/kgsl-3d0/devfreq/available_frequencies 2>/dev/null
echo -n "GPU gov: "; cat /sys/class/kgsl/kgsl-3d0/devfreq/governor 2>/dev/null
echo -n "GPU min freq: "; cat /sys/class/kgsl/kgsl-3d0/devfreq/min_freq 2>/dev/null
echo -n "GPU max freq: "; cat /sys/class/kgsl/kgsl-3d0/devfreq/max_freq 2>/dev/null
echo -n "GPU default pwrlevel: "; cat /sys/class/kgsl/kgsl-3d0/default_pwrlevel 2>/dev/null
echo -n "GPU num pwrlevels: "; cat /sys/class/kgsl/kgsl-3d0/num_pwrlevels 2>/dev/null
echo -n "GPU thermal pwrlevel: "; cat /sys/class/kgsl/kgsl-3d0/thermal_pwrlevel 2>/dev/null
echo -n "GPU bus dcvs: "; cat /sys/class/kgsl/kgsl-3d0/bus_split 2>/dev/null
echo -n "GPU adreno_boost: "; cat /sys/class/kgsl/kgsl-3d0/devfreq/adreno_boost 2>/dev/null

echo ""
echo "=== FAN AND AYN PLATFORM NODES ==="
find /sys/devices/platform -maxdepth 3 2>/dev/null | grep -iE "fan|ayn|retro|odin"
for f in /sys/devices/platform/*fan*/*; do
  if [ -f "$f" ]; then
    echo "$f: $(cat "$f" 2>/dev/null)"
  fi
done

echo ""
echo "=== MEMORY AND VM ==="
free -m
cat /proc/meminfo | grep -E "MemTotal|MemFree|MemAvailable|SwapTotal|SwapFree|Zram"
echo -n "swappiness: "; cat /proc/sys/vm/swappiness
echo -n "vfs_cache_pressure: "; cat /proc/sys/vm/vfs_cache_pressure
echo -n "dirty_ratio: "; cat /proc/sys/vm/dirty_ratio
echo -n "dirty_background_ratio: "; cat /proc/sys/vm/dirty_background_ratio
echo -n "stat_interval: "; cat /proc/sys/vm/stat_interval 2>/dev/null
echo -n "compaction_proactiveness: "; cat /proc/sys/vm/compaction_proactiveness 2>/dev/null
echo -n "watermark_boost_factor: "; cat /proc/sys/vm/watermark_boost_factor 2>/dev/null
echo -n "watermark_scale_factor: "; cat /proc/sys/vm/watermark_scale_factor 2>/dev/null

echo ""
echo "=== THERMAL ZONES ==="
for tz in /sys/class/thermal/thermal_zone[0-9]*; do
  type=$(cat "$tz/type" 2>/dev/null)
  temp=$(cat "$tz/temp" 2>/dev/null)
  case "$type" in
    *cpu*|*gpu*|*skin*|*battery*|*soc*|*quiet*)
      echo "$tz ($type): $temp"
      ;;
  esac
done

echo ""
echo "=== STORAGE AND IO ==="
for d in /sys/block/sd*/queue; do
  echo "$d scheduler: $(cat "$d/scheduler" 2>/dev/null)"
  echo "$d read_ahead_kb: $(cat "$d/read_ahead_kb" 2>/dev/null)"
  echo "$d nr_requests: $(cat "$d/nr_requests" 2>/dev/null)"
  echo "$d rq_affinity: $(cat "$d/rq_affinity" 2>/dev/null)"
done

echo ""
echo "=== DISPLAY AND REFRESH RATE ==="
dumpsys display | grep -E "mBaseDisplayInfo|mDefaultModeId|supportedModes" | head -n 10
dumpsys SurfaceFlinger | grep -E "Refresh Rate|fps" | head -n 10
