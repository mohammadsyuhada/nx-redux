#!/bin/sh
# NAME: Virtual Boy (mednafen_vb)
# Pre-launch options editor entry point (minarch core options). Its existence
# is the capability marker nxredux probes for the "Emulator Options"
# context-menu entry, and the Emulator Settings picker lists it.
# Usage: options.sh [rom-path]   (no arg: edit the console-wide config)

EMU_EXE=mednafen_vb
CORES_PATH="$(dirname "$0")"

###############################

PAK_DIR="$(dirname "$0")"
EMU_TAG="$(basename "$PAK_DIR" .pak)"
CORE="$CORES_PATH/${EMU_EXE}_libretro.so"
CORE_BASE="$(basename "$CORE")"
CORE_NAME="${CORE_BASE%_*}"
CONFIG_DIR="$USERDATA_PATH/$EMU_TAG-$CORE_NAME"
CACHE="$CONFIG_DIR/options.json"
mkdir -p "$CONFIG_DIR"

# Schema cache: regenerate when missing or older than the core .so. A failed
# dump leaves any existing (stale) cache usable; with no cache at all
# options.elf shows its "Settings unavailable" message.
if [ ! -f "$CACHE" ] || [ "$CORE" -nt "$CACHE" ]; then
    minarch.elf --dump-options "$CORE" "$CACHE" >> "$LOGS_PATH/emu-options.txt" 2>&1
fi

# Read-only lower layers, device variant preferred (mirrors Config_load,
# ma_config.c:1349-1381). The user tier's strict -$DEVICE suffix is handled
# inside options.elf.
SYS_CFG="$SYSTEM_PATH/system.cfg"
[ -n "${DEVICE:-}" ] && [ -f "$SYSTEM_PATH/system-$DEVICE.cfg" ] && SYS_CFG="$SYSTEM_PATH/system-$DEVICE.cfg"
DEF_CFG="$PAK_DIR/default.cfg"
[ -n "${DEVICE:-}" ] && [ -f "$PAK_DIR/default-$DEVICE.cfg" ] && DEF_CFG="$PAK_DIR/default-$DEVICE.cfg"

# Per-game key: minarch's alt_name — the basename WITH extension, collapsed
# to the folder-named .m3u for multi-disc folder games (ma_game.c:110-136).
nx_minarch_alt_name() {
    _dir="$(dirname "$1")"
    _dirbase="${_dir##*/}"
    if [ -f "$_dir/$_dirbase.m3u" ]; then
        printf '%s.m3u' "$_dirbase"
    else
        printf '%s' "${1##*/}"
    fi
}

if [ -n "${1:-}" ]; then
    ALT="$(nx_minarch_alt_name "$1")"
    options.elf --json "$CACHE" \
        --minarch-system "$SYS_CFG" --minarch-default "$DEF_CFG" \
        --minarch-dir "$CONFIG_DIR" --minarch-game "$ALT" \
        --game "$ALT" 2>> "$LOGS_PATH/emu-options.txt"
else
    options.elf --json "$CACHE" \
        --minarch-system "$SYS_CFG" --minarch-default "$DEF_CFG" \
        --minarch-dir "$CONFIG_DIR" \
        --game "Virtual Boy (mednafen_vb)" 2>> "$LOGS_PATH/emu-options.txt"
fi
