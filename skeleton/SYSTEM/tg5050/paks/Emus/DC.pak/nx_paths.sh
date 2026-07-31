#!/bin/sh
# Shared by launch.sh and options.sh (sourced, not executed): resolve the
# per-device config dir and seed emu.cfg on first use, so options can be
# edited before a game has ever launched without the two scripts drifting.
# Expects PAK_DIR to be set by the sourcing script.

# User data (config per device, VMU + save states shared across devices)
USERDATA_DIR="$SHARED_USERDATA_PATH/DC-flycast"
DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5050"
DEVICE_DEFAULT_CFG="$PAK_DIR/default.cfg"
mkdir -p "$DEVICE_CONFIG_DIR/flycast/mappings" "$USERDATA_DIR/data/flycast"

# First run: copy device-specific defaults
if [ ! -f "$DEVICE_CONFIG_DIR/.initialized" ]; then
    cp "$DEVICE_DEFAULT_CFG" "$DEVICE_CONFIG_DIR/flycast/emu.cfg"
    touch "$DEVICE_CONFIG_DIR/.initialized"
fi
EMU_CFG="$DEVICE_CONFIG_DIR/flycast/emu.cfg"

# Per-game override key. Multi-disc games live in a folder with a
# folder-named .m3u (see BASE README); every disc of such a game maps to
# ONE key -- the m3u/folder name -- mirroring how minarch normalizes
# alt_name for its own per-game config (ma_game.c:110-136), except that
# this pak's key convention drops the extension. Without this, the editor
# (handed the .m3u by nxredux) and launch.sh (handed a resolved first-disc
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
