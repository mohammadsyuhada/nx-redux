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
exec "$SYS/bin/nextui.elf" >> "$LOGS_PATH/nextui.txt" 2>&1
