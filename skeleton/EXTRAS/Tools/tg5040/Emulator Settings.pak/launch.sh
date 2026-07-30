#!/bin/sh

cd "$(dirname "$0")"

# Low fixed frequency for simple UI
echo 600000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

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

# BASE minarch paks live under the system paks dir, not on the SD card's
# Emus/<platform>/ tree — scan those too. An SD pak with the same tag wins
# (matches getEmuPath's precedence, utils.c:439-444).
SYS_PAKS="${SYSTEM_PATH:-${CORES_PATH%/cores}}/paks"
for opt in "$SYS_PAKS/Emus"/*.pak/options.sh; do
    [ -f "$opt" ] || continue
    tag="$(basename "$(dirname "$opt")" .pak)"
    [ -f "$SDCARD_PATH/Emus/$NX_PLATFORM/$tag.pak/options.sh" ] && continue
    name="$(sed -n 's/^# NAME: //p' "$opt" | head -1)"
    [ -n "$name" ] || name="$tag"
    set -- "$@" --entry "$name" "$opt"
done

# Nothing to configure (no emulator pak ships options.sh on this card)
[ $# -eq 0 ] && exit 0

# Picker prints the chosen options.sh on stdout; empty means cancelled (B).
# Loop rather than exec: closing an emulator's editor returns to this list,
# so several emulators can be configured in one visit. Each iteration
# restarts the picker at the top of the list (options.elf keeps no cursor
# state between runs).
while CHOSEN="$(options.elf --pick "$@" 2>> "$LOGS_PATH/emu-settings.txt")" && [ -n "$CHOSEN" ]; do
    "$CHOSEN"
done
