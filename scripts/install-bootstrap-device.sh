#!/system/bin/sh
set -eu
magisk --install-module /data/local/tmp/ThorNativeFG-Zygisk-experimental.zip
module_dir=/data/adb/modules_update/thor_native_fg_bootstrap
if [ ! -d "$module_dir" ]; then module_dir=/data/adb/modules/thor_native_fg_bootstrap; fi
test -f "$module_dir/zygisk/arm64-v8a.so"
magisk --sqlite "INSERT OR REPLACE INTO settings (key,value) VALUES('zygisk',1);"
magisk --sqlite "SELECT key,value FROM settings WHERE key='zygisk';"
echo THOR_BOOTSTRAP_INSTALLED_REBOOT_REQUIRED
