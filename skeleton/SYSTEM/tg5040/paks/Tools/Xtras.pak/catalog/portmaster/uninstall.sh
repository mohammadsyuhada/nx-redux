#!/bin/sh
# Xtras catalog: PortMaster keep-ports uninstall.
# Contract: run with the same env as install.sh, streamed the same way.
# Removes the Tools pak FIRST (reverse of install's hide-on-broken
# ordering), then the version marker, then the runtime, then the Ports
# console launcher. PRESERVES user data: installed ports and their saves in
# Roms/Ports (PORTS) are never touched (they need the runtime to run, but a
# reinstall brings them all straight back), and the BASE-shipped
# Emus/shared/PortMaster/{files,patchedScripts}/ stay - they belong to the
# card, not this entry (the old elf's cleanup_portmaster() kept them too).
# Idempotent (safe on an already/partially/never-installed entry - every
# removal is a no-op-safe rm -f/-rf). Needs no network access at all.
# Progress hints: same "@NN status text" convention as install.sh.
set -u

PM_DIR="$SDCARD_PATH/Emus/shared/PortMaster"
PORTS_PAK="$SDCARD_PATH/Emus/PORTS.pak"
# Pre-flattening installs placed PORTS.pak under Emus/<platform>/
LEGACY_PORTS_PAK="$SDCARD_PATH/Emus/$PLATFORM/PORTS.pak"
TOOLS_PAK="$SDCARD_PATH/Tools/PortMaster.pak"
# Earlier revisions of this entry installed to the platform subfolder.
OLD_TOOLS_PAK="$SDCARD_PATH/Tools/$PLATFORM/PortMaster.pak"
: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"
USERDATA_DIR="$SDCARD_PATH/.userdata/$PLATFORM"

echo "@10 Removing PortMaster tool..."
echo "Removing PortMaster tool..."
rm -rf "$TOOLS_PAK" "$OLD_TOOLS_PAK"
# rmdir (not rm -rf) the possibly-empty platform dir so unrelated user paks
# survive; ignore failure (not empty/never existed).
rmdir "$SDCARD_PATH/Tools/$PLATFORM" 2>/dev/null || true

echo "@30 Removing version marker..."
echo "Removing version marker..."
rm -f "$XTRAS_STATE_DIR/portmaster.version"

echo "@50 Removing runtime..."
echo "Removing runtime..."
for entry in "$PM_DIR"/* "$PM_DIR"/.[!.]*; do
    [ -e "$entry" ] || continue
    case "$(basename "$entry")" in
        files|patchedScripts) ;;
        *) rm -rf "$entry" ;;
    esac
done

echo "@80 Removing Ports console..."
echo "Removing Ports console..."
rm -rf "$PORTS_PAK" "$LEGACY_PORTS_PAK"
# rmdir (not rm -rf) the now-possibly-empty legacy platform dir so any
# unrelated content there survives; ignore failure (not empty/never existed).
rmdir "$SDCARD_PATH/Emus/$PLATFORM" 2>/dev/null || true

# Hide the Ports console and the Tools pak without a reboot.
rm -f "$USERDATA_DIR/emulist_cache.txt" "$USERDATA_DIR/romindex_cache.txt"

echo "@100 Done"
echo "Done. Installed ports and saves kept - reinstall to play them again."
exit 0
