#!/system/bin/sh
set -eu
bin='/data/user/0/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/steamapps/common/Foxtale Demo/LittleFoxSim_v1/Binaries/Win64'
backup=/data/adb/thor-native-fg/foxtale-gsr2-test-v1
test -f "$backup/ready"
if pgrep -f '[L]ittleFoxSim_v1-Win64-Shipping.exe' >/dev/null; then echo 'Game is running; no files changed'; exit 1; fi
sha256sum -c "$backup/installed.sha256"
sha256sum -c "$backup/executable-before.sha256"
test -f "$bin/dxgi.dll" && test -f "$bin/OptiScaler.ini"
rm "$bin/dxgi.dll" "$bin/OptiScaler.ini"
mv "$backup/ready" "$backup/undone"
echo 'Removed only the two verified Thor test files. Clear the Foxtale dxgi launch override in Steam.'
