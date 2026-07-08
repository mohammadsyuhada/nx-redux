#!/bin/sh
# Power off. Follows the system contract (see MinUI.pak/launch.sh): remove
# /tmp/nextui_exec so the launch loop stops respawning nextui, flag
# /tmp/poweroff, then terminate the foreground app — the loop then runs
# poweroff_next. Note: pressing this in-game exits minarch without a
# quicksave (same as a hard power cut, minus filesystem risk).
STATUS_DIR=/tmp/trimui_osd/toggle_power
mkdir -p $STATUS_DIR
echo 0 > $STATUS_DIR/status

# no-arg call = status query only
[ $# -eq 0 ] && exit 0

# close the OSD for a clean shutdown screen
touch /tmp/hide_osdd
sync
rm -f /tmp/nextui_exec
touch /tmp/poweroff
killall nextui.elf minarch.elf 2>/dev/null
# Safety net: if no app was running to unwind the launch loop, power off directly
( sleep 10; poweroff ) &
