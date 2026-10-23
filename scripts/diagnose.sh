#!/system/bin/sh
echo "=== SEARCH SYS FAN & AYN ==="
find /sys -iname "*fan*" 2>/dev/null
find /sys -iname "*odin*" 2>/dev/null
find /sys -iname "*aync*" 2>/dev/null
find /sys -iname "*retro*" 2>/dev/null
find /sys/class -maxdepth 2 2>/dev/null

echo "=== CPU FREQUENCIES DETAILS ==="
for i in 0 3 7; do
  echo "cpu$i scaling_available_frequencies:"
  cat /sys/devices/system/cpu/cpu$i/cpufreq/scaling_available_frequencies 2>/dev/null
  echo "cpu$i cpuinfo_min_freq:"
  cat /sys/devices/system/cpu/cpu$i/cpufreq/cpuinfo_min_freq 2>/dev/null
  echo "cpu$i cpuinfo_max_freq:"
  cat /sys/devices/system/cpu/cpu$i/cpufreq/cpuinfo_max_freq 2>/dev/null
  echo "cpu$i scaling_min_freq:"
  cat /sys/devices/system/cpu/cpu$i/cpufreq/scaling_min_freq 2>/dev/null
  echo "cpu$i scaling_max_freq:"
  cat /sys/devices/system/cpu/cpu$i/cpufreq/scaling_max_freq 2>/dev/null
done

echo "=== THERMAL ENGINE & DAEMONS ==="
ps -ef | grep -iE "thermal|perf|mpdecision|qualcomm"

echo "=== SYSTEM PROPERTIES PERF & THERMAL ==="
getprop | grep -iE "fan|perf|mode|thermal|game|fps|power"

echo "=== SCREEN MODES & REFRESH RATE SETTINGS ==="
settings get system peak_refresh_rate 2>/dev/null
settings get system min_refresh_rate 2>/dev/null
settings get global user_refresh_rate 2>/dev/null
settings list system | grep -iE "refresh|fan|mode|perf|game|thor"
settings list global | grep -iE "refresh|fan|mode|perf|game|thor"

echo "=== ANIMATION SCALES ==="
settings get global window_animation_scale
settings get global transition_animation_scale
settings get global animator_duration_scale
