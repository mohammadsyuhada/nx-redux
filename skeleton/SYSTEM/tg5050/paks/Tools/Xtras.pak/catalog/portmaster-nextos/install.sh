#!/bin/sh
# Xtras catalog: NextOS Universal Ports, a third-party PortMaster catalog.
# Drops the catalog's source file into PortMaster's config/, where
# harbourmaster loads every *.source.json; PortMaster then lists the ports
# and refreshes the catalog itself (hourly, when opened), so new ports need
# no update here. A version bump here only re-copies the source file (e.g.
# a moved catalog URL); the copy also resets harbourmaster's cache in it,
# which makes the next PortMaster open re-fetch the catalog.
# It does NOT write the version marker: this is an internal entry, so
# extras.elf writes $XTRAS_STATE_DIR/portmaster-nextos.version after a
# successful install. Runs under extras.elf's scrubbed env (SDCARD_PATH/
# PLATFORM/LOGS_PATH/CATALOG_DIR/XTRAS_STATE_DIR). No network.
set -u

PM_DIR="$SDCARD_PATH/Emus/shared/PortMaster"
SRC="040_nextos.source.json"

fail() { echo "ERROR: $1"; exit 1; }

[ -f "$CATALOG_DIR/$SRC" ] || fail "catalog payload missing ($SRC)"
[ -f "$PM_DIR/pugwash" ] || fail "install PortMaster first (Xtras, Tools tab)"

echo "@50 Adding the NextOS catalog to PortMaster..."
mkdir -p "$PM_DIR/config" || fail "cannot create PortMaster's config folder"
cp -f "$CATALOG_DIR/$SRC" "$PM_DIR/config/$SRC" || fail "could not add the catalog"

echo "@100 Done"
echo "Added. Open PortMaster to browse the NextOS ports."
exit 0
