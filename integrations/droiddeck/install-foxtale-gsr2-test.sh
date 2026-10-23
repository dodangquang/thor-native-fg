#!/system/bin/sh
set -eu
base=/data/user/0/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/steamapps
game="$base/common/Foxtale Demo"
bin="$game/LittleFoxSim_v1/Binaries/Win64"
source=/data/local/tmp/thor-gsr2-test
backup=/data/adb/thor-native-fg/foxtale-gsr2-test-v1
test -f "$bin/LittleFoxSim_v1-Win64-Shipping.exe"
test "$(stat -c %u /data/user/0/com.droiddeck.launcher)" = 10201
if pgrep -f '[L]ittleFoxSim_v1-Win64-Shipping.exe' >/dev/null; then echo 'Game is running; no files changed'; exit 1; fi
test ! -e "$bin/dxgi.dll"
test ! -e "$bin/OptiScaler.ini"
test ! -e "$backup"
test "$(sha256sum "$source/dxgi.dll" | cut -d ' ' -f1)" = 8832d615be41375ab85f5c1c7a122f2924fc71559cf47966a4704f14d781c4ff
test "$(sha256sum "$source/OptiScaler.ini" | cut -d ' ' -f1)" = f17ae55f5a9b91a1e16f469a889ef215ae5a4d41356601991ca21279b912b04c
umask 077
mkdir "$backup"
sha256sum "$bin/LittleFoxSim_v1-Win64-Shipping.exe" > "$backup/executable-before.sha256"
printf '%s\n' 'dxgi.dll absent' 'OptiScaler.ini absent' > "$backup/before.txt"
cp "$source/dxgi.dll" "$bin/dxgi.dll"
cp "$source/OptiScaler.ini" "$bin/OptiScaler.ini"
chmod 644 "$bin/dxgi.dll" "$bin/OptiScaler.ini"
chown 10201:10201 "$bin/dxgi.dll" "$bin/OptiScaler.ini"
restorecon -F "$bin/dxgi.dll" "$bin/OptiScaler.ini"
sha256sum "$bin/dxgi.dll" "$bin/OptiScaler.ini" > "$backup/installed.sha256"
touch "$backup/ready"
echo 'Foxtale SGSR2-only files installed. Steam launch options still need dxgi=n,b. No saves changed.'
