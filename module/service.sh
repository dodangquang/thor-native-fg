#!/system/bin/sh
# Runs at Magisk late_start after this version has been loaded into zygote.
setprop sys.thorfg.bootstrap 2
THOR_CONFIG=/data/adb/thor-native-fg/auto.conf
setprop debug.thorfg.auto 0
setprop debug.thorfg.auto_sr 0
if [ -f "$THOR_CONFIG" ] && grep -qx 'enabled=1' "$THOR_CONFIG"; then
  if grep -qx 'sr=1' "$THOR_CONFIG"; then setprop debug.thorfg.auto_sr 1; fi
  setprop debug.thorfg.auto 1
  while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 2; done
  # The service checks the existing overlay permission and stops if unavailable.
  am start-foreground-service -n com.thor.nativefg/.FpsOverlayService >/dev/null 2>&1
fi
