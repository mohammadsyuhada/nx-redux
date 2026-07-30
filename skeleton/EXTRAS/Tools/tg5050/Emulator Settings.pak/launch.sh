#!/bin/sh

cd "$(dirname "$0")"

# Idle big core at minimum, little cores auto-scale via schedutil
echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq 2>/dev/null

# …/Tools/<platform>/Emulator Settings.pak -> <platform>
NX_PLATFORM="$(basename "$(dirname "$(pwd)")")"

# Emulator paks opt in by shipping an options.sh (also the marker nextui
# probes for the game list's "Emulator Options" context-menu entry).
# Display name comes from its "# NAME:" line, falling back to the pak name.
set --
for opt in "$SDCARD_PATH/Emus/$NX_PLATFORM"/*.pak/options.sh; do
    [ -f "$opt" ] || continue
    name="$(sed -n 's/^# NAME: //p' "$opt" | head -1)"
    [ -n "$name" ] || name="$(basename "$(dirname "$opt")" .pak)"
    set -- "$@" --entry "$name" "$opt"
done

# Nothing to configure (no emulator pak ships options.sh on this card)
[ $# -eq 0 ] && exit 0

# Picker prints the chosen options.sh on stdout; empty means cancelled.
CHOSEN="$(options.elf --pick "$@" 2> "$LOGS_PATH/emu-settings.txt")"
[ -n "$CHOSEN" ] && exec "$CHOSEN"
