#!/bin/sh
# Xtras catalog: Cheat Database (CODE side). Copies the launchable pak payload
# into Tools/ and busts the launcher list caches so it appears without reboot.
# It does NOT download the cheats - the pak (Tools/Cheat Database.pak) does that
# on demand. It does NOT write the version marker: this is an internal entry, so
# extras.elf writes $XTRAS_STATE_DIR/cheatdb.version after a successful install.
# Runs under extras.elf's scrubbed env (SDCARD_PATH/PLATFORM/LOGS_PATH/
# CATALOG_DIR/XTRAS_STATE_DIR). No network.
set -u

TOOLS_PAK="$SDCARD_PATH/Tools/Cheat Database.pak"
USERDATA_DIR="$SDCARD_PATH/.userdata/$PLATFORM"

fail() { echo "ERROR: $1"; exit 1; }

[ -f "$CATALOG_DIR/pak/launch.sh" ] || fail "catalog pak payload missing (launch.sh)"

echo "@40 Installing Cheat Database tool..."
mkdir -p "$TOOLS_PAK" || fail "cannot create the Cheat Database pak"
cp -f "$CATALOG_DIR/pak/launch.sh" "$TOOLS_PAK/launch.sh" || fail "could not copy the pak launcher"
chmod +x "$TOOLS_PAK/launch.sh" 2>/dev/null || true

echo "@90 Refreshing tools list..."
rm -f "$USERDATA_DIR/emulist_cache.txt" "$USERDATA_DIR/romindex_cache.txt"

echo "@100 Done"
echo "Installed. Open Cheat Database in Tools to download the cheats."
exit 0
