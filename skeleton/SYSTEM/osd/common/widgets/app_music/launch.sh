#!/bin/sh
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
WIDGET=/mnt/SDCARD/.system/bin/osdmusic.elf
DIR=/tmp/trimui_music
READY=$DIR/widget_ready
CMD=$DIR/widget_cmd
CANVAS=$DIR/vfb_osd
LOCK=$DIR/widget.lock
CONFIG=/usr/trimui/osd/widgets/app_music/config.json

canvas_w=$(awk -F: '/"canvaswidth"/ {gsub(/[^0-9]/, "", $2); print $2; exit}' "$CONFIG")
canvas_h=$(awk -F: '/"canvasheight"/ {gsub(/[^0-9]/, "", $2); print $2; exit}' "$CONFIG")
[ -n "$canvas_w" ] && [ -n "$canvas_h" ] || exit 1
canvas_bytes=$((canvas_w * canvas_h * 4))

ready() {
	pid=$(cat "$READY" 2>/dev/null)
	[ -n "$pid" ] && [ -d "/proc/$pid" ] && kill -0 "$pid" 2>/dev/null &&
		[ -e "$LOCK" ] && [ -p "$CMD" ] && [ -f "$CANVAS" ] &&
		[ "$(wc -c <"$CANVAS" 2>/dev/null)" -eq "$canvas_bytes" ] &&
		[ -r "/proc/$pid/cmdline" ] &&
		tr '\000' ' ' <"/proc/$pid/cmdline" 2>/dev/null | grep -q '[o]sdmusic.elf'
}

mkdir -p "$DIR"
if ready; then
	exit 0
fi
rm -f "$READY"
[ -x "$WIDGET" ] || exit 1
"$WIDGET" </dev/null >/dev/null 2>&1 &

tries=0
while [ "$tries" -lt 100 ]; do
	ready && exit 0
	tries=$((tries + 1))
	usleep 20000 2>/dev/null || sleep 0.02
done
exit 1
