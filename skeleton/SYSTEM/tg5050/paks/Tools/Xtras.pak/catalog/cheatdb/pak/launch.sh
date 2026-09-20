#!/bin/sh
# Tools/Cheat Database.pak/launch.sh - launches the Cheat Database app.
# The app (cheatdb.elf) owns download/update/remove of the libretro cheat data;
# the small pak CODE is installed/updated via Xtras. Runs with the full MinUI
# pak env. Busybox sh.
cd "$(dirname "$0")"
rm -f "$LOGS_PATH/cheatdb.txt"
exec >"$LOGS_PATH/cheatdb.txt" 2>&1

# CPU: a UI that downloads and unpacks zips — the same profile as the other
# Tools paks, not a game. tg5050: big core offline (the launcher hands it over
# online, see MinUI.pak/launch.sh), little cores at their default range.
# tg5040: same 1008 MHz schedutil cap as the launcher menu.
case "$PLATFORM" in
    tg5050) echo 0 > /sys/devices/system/cpu/cpu4/online 2>/dev/null ;;
    *)      echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null ;;
esac

if [ ! -x "./cheatdb.elf" ]; then
    echo "cheatdb.elf missing"
    exit 1
fi
exec ./cheatdb.elf
