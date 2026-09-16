#!/bin/sh
# Xtras catalog: gen1recomp keep-saves uninstall.
# Contract: run with the same env as install.sh, streamed the same way.
# Removes the visible launcher FIRST (reverse of install's register-last),
# then the version marker, then the engine/runtime payload. PRESERVES user
# data: lovegame/red|blue|yellow (ROM save caches), the user's own *.gb/
# *.gbc ROMs, lovegame/options.lua, and conf/ - none of those paths are
# touched below. Idempotent (safe on an already-uninstalled, partially
# uninstalled, or never-installed entry - every removal is a no-op-safe
# rm -f/-rf). Exit code is the verdict; every step prints a progress line.
# Needs no network access at all, so (unlike install.sh) no command here
# needs a timeout - only busybox rm/printf-safe commands are used.
# Progress hints (Task 11): same "@NN status text" convention as install.sh -
# see its header for the full contract text.
set -u

TARGET="$EXTRAS_DATA_DIR/gen1recomp"
LOVE="$TARGET/lovegame"
: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"

echo "@10 Removing launcher..."
echo "Removing launcher..."
rm -f "$EXTRAS_ROMS_DIR/Gen1recomp.sh"

# Drop only this entry's display-alias line from the shared map.txt (see
# install.sh: filename<TAB>alias); other entries' lines are kept, and the
# file itself is removed once nothing is left in it. Idempotent: a missing
# map.txt or an already-removed line is a no-op.
MAP="$EXTRAS_ROMS_DIR/map.txt"
TAB="$(printf '\t')"
if [ -f "$MAP" ]; then
    grep -v "^Gen1recomp\.sh$TAB" "$MAP" > "$MAP.tmp"
    if [ -s "$MAP.tmp" ]; then
        mv "$MAP.tmp" "$MAP"
    else
        rm -f "$MAP.tmp" "$MAP"
    fi
fi

echo "@30 Removing version marker..."
echo "Removing version marker..."
rm -f "$TARGET/.nx_addon_version"
rm -f "$XTRAS_STATE_DIR/gen1recomp.version"

echo "@55 Removing engine..."
echo "Removing engine..."
rm -rf "${TARGET:?}/bin" "${TARGET:?}/libs.aarch64" "${TARGET:?}/licenses"

echo "@80 Removing game runtime..."
echo "Removing game runtime..."
rm -rf "${LOVE:?}/src" "${LOVE:?}/data" "${LOVE:?}/assets" "${LOVE:?}/libs" "${LOVE:?}/tools" "${LOVE:?}/mods"
rm -f "$LOVE/main.lua" "$LOVE/conf.lua" "$LOVE/portable.txt"

# Legacy swapfile cleanup (2026-09-16): earlier launchers created an eMMC
# swapfile for the voxel overworld mods (/swapfile, later /mnt/UDISK/
# swapfile[.new]). Those mods are no longer bundled and the base game needs
# no swap (116MB peak RSS in play on the Brick), so reclaim the space -
# the last version cost 2GB of UDISK plus eMMC wear. Non-fatal throughout.
# NX_SWAP_ROOT is a test seam (the host test can't touch the real /).
for _sf in "${NX_SWAP_ROOT:-}/swapfile" "${NX_SWAP_ROOT:-}/mnt/UDISK/swapfile" "${NX_SWAP_ROOT:-}/mnt/UDISK/swapfile.new"; do
    [ -e "$_sf" ] || continue
    if grep -q "^$_sf " /proc/swaps 2>/dev/null; then
        swapoff "$_sf" 2>/dev/null || { echo "  swapfile $_sf still in use - left in place"; continue; }
    fi
    rm -f "$_sf" && echo "Removed legacy swapfile $_sf"
done

echo "@100 Done"
echo "Done. Saves and ROMs kept - reinstall to play again."
exit 0
