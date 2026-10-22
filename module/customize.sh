#!/system/bin/sh
ui_print "Thor Native FG bootstrap 0.2 (experimental)"
ui_print "Only the manual target or the user's automatic UID/package list is eligible."
ui_print "Enable Zygisk in Magisk and reboot once."
ui_print "The Vulkan APK and a compatible Vulkan renderer are also required."
if [ "$ARCH" != "arm64" ]; then
  abort "This build supports arm64 Android only."
fi
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0644
set_perm "$MODPATH/service.sh" 0 0 0755
