#!/bin/sh
# Shared by launch.sh and options.sh (sourced, not executed): resolve the
# config dir and seed mupen64plus.cfg on first use, so options can be
# edited before a game has ever launched without the two scripts drifting.
# Expects PAK_DIR to be set by the sourcing script.

# User data directory, shared across devices (saves via XDG_DATA_HOME in
# launch.sh; config stays per-device in subdirs)
USERDATA_DIR="$SHARED_USERDATA_PATH/N64-mupen64plus"
mkdir -p "$USERDATA_DIR"

# Device-specific config directory
DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5050"
mkdir -p "$DEVICE_CONFIG_DIR"

# First run: copy device-specific defaults
if [ ! -f "$DEVICE_CONFIG_DIR/.initialized" ]; then
    cp "$PAK_DIR/default.cfg" "$DEVICE_CONFIG_DIR/mupen64plus.cfg"
    touch "$DEVICE_CONFIG_DIR/.initialized"
fi
EMU_CFG="$DEVICE_CONFIG_DIR/mupen64plus.cfg"

# Per-game override key. Multi-disc games live in a folder with a
# folder-named .m3u (see BASE README); every disc of such a game maps to
# ONE key -- the m3u/folder name -- mirroring how minarch normalizes
# alt_name for its own per-game config (ma_game.c:110-136), except that
# this pak's key convention drops the extension. Without this, the editor
# (handed the .m3u by nextui) and launch.sh (handed a resolved first-disc
# path by openRom) would write and read different override files.
nx_rom_base() {
    _dir="$(dirname "$1")"
    _dirbase="${_dir##*/}"
    if [ -f "$_dir/$_dirbase.m3u" ]; then
        printf '%s' "$_dirbase"
    else
        _b="${1##*/}"
        printf '%s' "${_b%.*}"
    fi
}
