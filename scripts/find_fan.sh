#!/system/bin/sh
echo "=== MAX31760 SEARCH ==="
find /sys/bus/i2c -name "*31760*" 2>/dev/null
find /sys/devices -name "*31760*" 2>/dev/null
for dev in /sys/class/thermal/cooling_device*; do
  c_type=$(cat "$dev/type" 2>/dev/null)
  c_cur=$(cat "$dev/cur_state" 2>/dev/null)
  c_max=$(cat "$dev/max_state" 2>/dev/null)
  echo "$dev: type=$c_type cur=$c_cur max=$c_max"
done | grep -i fan
