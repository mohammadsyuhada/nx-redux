#!/bin/sh
# Xtras catalog: PPSSPP keep-saves uninstall.
# Contract: run with the same env as install.sh, streamed the same way.
# Removes the pak's launcher FIRST (reverse of install's hide-on-broken
# ordering), then the version marker, then the emulator payload, then any
# legacy flat Emus/PSP.pak copy. PRESERVES user data: Saves/PSP and
# the shared savestate dir both live OUTSIDE the pak (launch.sh bind-mounts
# them in), games in Roms/Sony Playstation Portable (PSP) are never touched,
# the pak's PPSSPP/.config tree (in-emulator settings, texture packs, cheat
# toggles) is deliberately kept - a later reinstall detects and reuses it.
# Idempotent (safe on an already/partially/never-installed entry - every
# removal is a no-op-safe rm -f/-rf). Needs no network access at all.
# Progress hints: same "@NN status text" convention as install.sh.
set -u

TARGET="$SDCARD_PATH/Emus/$PLATFORM/PSP.pak"
OLD_PAK="$SDCARD_PATH/Emus/PSP.pak" # legacy flat install (before 2026-09-16)
: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"

echo "@10 Removing launcher..."
echo "Removing launcher..."
rm -f "$TARGET/launch.sh" "$TARGET/pak.json" "$TARGET/LICENSE"

echo "@30 Removing version marker..."
echo "Removing version marker..."
rm -f "$XTRAS_STATE_DIR/psp.version"

echo "@60 Removing emulator..."
echo "Removing emulator..."
rm -rf "${TARGET:?}/bin" "${TARGET:?}/lib" \
       "${TARGET:?}/PPSSPP/PPSSPPSDL_tg5040" "${TARGET:?}/PPSSPP/PPSSPPSDL_tg5050" \
       "${TARGET:?}/PPSSPP/assets" "${TARGET:?}/PPSSPP/LICENSE.TXT"
# Clear the empty shell too when no user config is left to preserve, and
# the platform folder if this was the only pak in it.
rmdir "$TARGET/PPSSPP" "$TARGET" "$SDCARD_PATH/Emus/$PLATFORM" 2>/dev/null || true

echo "@85 Removing legacy flat copy..."
echo "Removing legacy flat copy..."
rm -rf "$OLD_PAK"

echo "@100 Done"
echo "Done. Saves and ROMs kept - reinstall to play again."
exit 0
