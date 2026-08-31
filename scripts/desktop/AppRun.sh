#!/bin/sh
# AppImage entry point (installed as AppRun). $APPDIR is set by the runtime.
set -eu
APPDIR="${APPDIR:-$(cd "$(dirname "$0")" && pwd)}"
. "$APPDIR/usr/entry-common.sh"
entry_resolve_roots "$APPDIR/usr/system" "$APPDIR/usr/base-skeleton"
entry_seed_card
entry_export_env
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# $APPIMAGE (own file path) is exported by the AppImage runtime; OTA uses it.

# nextui.elf never execs a pak in place: opening a ROM/tool writes a shell
# command to /tmp/next and exits (nextui.c: "shell script reads /tmp/next
# only after nextui.elf exits"), by design, so the pak can take over the
# display/audio stack cleanly. Every real device has an outer boot-script
# loop that reads /tmp/next and relaunches nextui.elf once the pak returns;
# without that loop here the whole app would quit after opening one ROM.
# Mirror it. A clean quit (no /tmp/next written) ends the loop normally.
# `|| true` on both calls: under `set -e`, a pak/core that exits (or
# crashes) non-zero would otherwise kill this whole loop -- i.e. the entire
# desktop app -- instead of just falling back to the nextui.elf menu the way
# a real device does.
NEXT_PATH=/tmp/next
rm -f "$NEXT_PATH"
while :; do
	"$SYS/bin/nextui.elf" >> "$LOGS_PATH/nextui.txt" 2>&1 || true
	[ -f "$NEXT_PATH" ] || break
	CMD="$(cat "$NEXT_PATH")"
	rm -f "$NEXT_PATH"
	eval "$CMD" || true
done
