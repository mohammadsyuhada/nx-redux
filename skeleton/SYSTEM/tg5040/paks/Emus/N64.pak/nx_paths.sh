#!/bin/sh
# Shared by launch.sh and options.sh (sourced, not executed): resolve the
# per-device config dir and seed mupen64plus.cfg on first use, so options
# can be edited before a game has ever launched without the two scripts
# drifting. Expects PAK_DIR to be set by the sourcing script.

# User data directory, shared across devices (saves via XDG_DATA_HOME in
# launch.sh; config stays per-device in subdirs)
USERDATA_DIR="$SHARED_USERDATA_PATH/N64-mupen64plus"
mkdir -p "$USERDATA_DIR"

# Device-specific config directory
if [ "$DEVICE" = "brick" ]; then
    DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5040-brick"
    DEVICE_DEFAULT_CFG="$PAK_DIR/default-brick.cfg"
elif [ "$DEVICE" = "brickpro" ]; then
    # own config dir so a shared SD card doesn't carry tuning between the
    # Brick and the Brick Pro
    DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5040-brickpro"
    DEVICE_DEFAULT_CFG="$PAK_DIR/default-brickpro.cfg"
else
    DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5040-smart-pro"
    DEVICE_DEFAULT_CFG="$PAK_DIR/default-smartpro.cfg"
fi
mkdir -p "$DEVICE_CONFIG_DIR"

# First run: copy device-specific defaults
if [ ! -f "$DEVICE_CONFIG_DIR/.initialized" ]; then
    cp "$DEVICE_DEFAULT_CFG" "$DEVICE_CONFIG_DIR/mupen64plus.cfg"
    touch "$DEVICE_CONFIG_DIR/.initialized"
fi
EMU_CFG="$DEVICE_CONFIG_DIR/mupen64plus.cfg"

# Older seeds predate [NxRedux]; give existing installs the platform default
# without re-seeding, so the editor and launcher then read the same value.
if ! grep -q '^\[NxRedux\]' "$EMU_CFG"; then
    printf '\n' >> "$EMU_CFG"
    awk '/^\[NxRedux\]/{f=1;print;next} f&&/^\[/{exit} f{print}' \
        "$DEVICE_DEFAULT_CFG" >> "$EMU_CFG"
fi

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


# Selected video plugin: per-game cfg ($1) wins over the global cfg ($2),
# both read from [NxRedux] VideoPlugin (spaces around =, surrounding quotes
# and a trailing CR tolerated). Anything else -- missing file, absent key,
# unknown value -- falls through to gliden64, the safe default the launcher
# passes to --gfx. Kept awk simple for busybox (no gensub/-v regex tricks).
nx_video_plugin() {
    for _vpcfg in "$1" "$2"; do
        [ -f "$_vpcfg" ] || continue
        _vp=$(awk '
            { line = $0; sub(/\r$/, "", line) }
            line ~ /^[ \t]*\[/ {
                hdr = line; gsub(/[ \t]/, "", hdr)
                insec = (hdr == "[NxRedux]")
                next
            }
            insec {
                eq = index(line, "=")
                if (eq > 0) {
                    key = substr(line, 1, eq - 1); gsub(/[ \t]/, "", key)
                    if (key == "VideoPlugin") {
                        val = substr(line, eq + 1)
                        gsub(/^[ \t]+/, "", val); gsub(/[ \t]+$/, "", val)
                        gsub(/^"/, "", val); gsub(/"$/, "", val)
                        if (val == "rice" || val == "gliden64") { print val; exit }
                    }
                }
            }
        ' "$_vpcfg")
        if [ "$_vp" = "rice" ] || [ "$_vp" = "gliden64" ]; then
            printf '%s' "$_vp"
            return
        fi
    done
    printf '%s' gliden64
}
