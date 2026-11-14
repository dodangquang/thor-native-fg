#!/system/bin/sh
# Runs at Magisk late_start after this version has been loaded into zygote.
setprop sys.thorfg.bootstrap 3
THOR_CONFIG=/data/adb/thor-native-fg/auto.conf
setprop debug.thorfg.auto 0
setprop debug.thorfg.auto_sr 0
# debug.* is not persistent: restore the user's 3x choice saved by the
# controller so a reboot cannot silently fall back to 2x.
setprop debug.thorfg.triple 0
if [ "$(getprop persist.thorfg.triple)" = 1 ]; then setprop debug.thorfg.triple 1; fi
if [ -f "$THOR_CONFIG" ] && grep -qx 'enabled=1' "$THOR_CONFIG"; then
  provider_path=$(sed -n 's/^provider=//p' "$THOR_CONFIG")
  if [ ! -f "$provider_path/libVkLayer_ThorFG.so" ]; then
    log -t ThorNativeBootstrap 'AUTO_BYPASS boot runtime missing; automatic mode left off'
    exit 0
  fi
  if grep -qx 'sr=1' "$THOR_CONFIG"; then setprop debug.thorfg.auto_sr 1; fi
  setprop debug.thorfg.auto 1
  while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 2; done
  n_ext=/data/media/0/Android/data/dev.eden.eden_emulator.nightly/files
  if [ -d "$n_ext" ]; then
    if [ -f "$n_ext/tas" ]; then rm -f "$n_ext/tas"; fi
    mkdir -p "$n_ext/tas" "$n_ext/cache/shader" /data/media/0/Android/data/dev.eden.eden_emulator.relWithDebInfo/files
    chgrp -R 1078 "$n_ext" 2>/dev/null || true
    chmod -R g+rwX "$n_ext" 2>/dev/null || true
    find "$n_ext" -type d -exec chmod 2770 {} + 2>/dev/null || true
    for b in /data/media/0 /mnt/pass_through/0/emulated/0 /mnt/user/0/emulated/0 /mnt/installer/0/emulated/0 /mnt/androidwritable/0/emulated/0 /storage/emulated/0; do
      t="$b/Android/data/dev.eden.eden_emulator.relWithDebInfo/files"
      if [ -d "$t" ] && [ "$(stat -c %i "$t" 2>/dev/null)" != "$(stat -c %i "$n_ext" 2>/dev/null)" ]; then mount --bind "$n_ext" "$t" 2>/dev/null || true; fi
    done
    appops set dev.eden.eden_emulator.relWithDebInfo MANAGE_EXTERNAL_STORAGE allow 2>/dev/null || true
  fi
  # PackageManager is not necessarily ready at Magisk late_start. Check only
  # after Android finishes booting, so an early pm failure cannot disable auto.
  if [ -z "$(pm path com.thor.nativefg 2>/dev/null)" ]; then
    setprop debug.thorfg.auto 0
    setprop debug.thorfg.auto_sr 0
    exit 0
  fi
  # The service checks the existing overlay permission and stops if unavailable.
  am start-foreground-service -n com.thor.nativefg/.FpsOverlayService >/dev/null 2>&1
fi
