#!/bin/sh
# Music widget: trimui_osdd runs this once at startup, synchronously, and
# only then opens the widget's command FIFO and canvas (config.json "cmd" /
# "canvasv"). osdmusic.elf owns both and keeps drawing the widget in the
# background; it currently shows dummy content (no music owner exists yet)
# and is the reference contract for a real player to adopt.
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
WIDGET=/mnt/SDCARD/.system/bin/osdmusic.elf
READY=/tmp/trimui_music/widget_ready

mkdir -p /tmp/trimui_music
rm -f "$READY"
[ -x "$WIDGET" ] || exit 1
"$WIDGET" </dev/null >/dev/null 2>&1 &

# Return only once the canvas and FIFO exist, or trimui_osdd opens nothing.
tries=0
while [ "$tries" -lt 100 ]; do
	[ -s "$READY" ] && exit 0
	tries=$((tries + 1))
	usleep 20000 2>/dev/null || sleep 0.02
done
exit 1
