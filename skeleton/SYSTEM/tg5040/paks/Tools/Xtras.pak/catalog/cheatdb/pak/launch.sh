#!/bin/sh
# Tools/Cheat Database.pak/launch.sh - launches the Cheat Database app.
# The app (cheatdb.elf) owns download/update/remove of the libretro cheat data;
# the small pak CODE is installed/updated via Xtras. Runs with the full MinUI
# pak env. Busybox sh.
cd "$(dirname "$0")"
rm -f "$LOGS_PATH/cheatdb.txt"
exec >"$LOGS_PATH/cheatdb.txt" 2>&1

echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null

if [ ! -x "./cheatdb.elf" ]; then
    echo "cheatdb.elf missing"
    exit 1
fi
exec ./cheatdb.elf
