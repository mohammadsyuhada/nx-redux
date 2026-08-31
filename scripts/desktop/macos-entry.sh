#!/bin/sh
# NXRedux.app entry point (installed as Contents/MacOS/NXRedux).
set -eu
CONTENTS="$(cd "$(dirname "$0")/.." && pwd)"
. "$CONTENTS/Resources/entry-common.sh"
entry_resolve_roots "$CONTENTS/Resources/system" "$CONTENTS/Resources/base-skeleton"
entry_seed_card
entry_export_env
export NXREDUX_BUNDLE="$(dirname "$CONTENTS")" # the .app path, for OTA self-swap
exec "$SYS/bin/nextui.elf" >> "$LOGS_PATH/nextui.txt" 2>&1
