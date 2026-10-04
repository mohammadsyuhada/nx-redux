#!/bin/sh
# Xtras catalog: NextOS Universal Ports uninstall. Takes the catalog out of
# PortMaster (its source file and cached images) and drops the
# $XTRAS_STATE_DIR/portmaster-nextos.version marker. Ports already installed
# from it stay, with their saves; remove those from PortMaster.
# Self-contained; no network.
set -u

: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"
PM_DIR="$SDCARD_PATH/Emus/shared/PortMaster"

echo "@50 Removing the NextOS catalog from PortMaster..."
rm -f "$PM_DIR/config/040_nextos.source.json"
rm -rf "$PM_DIR/config/images_nextos"
rm -f "$XTRAS_STATE_DIR/portmaster-nextos.version"

echo "@100 Done"
echo "Removed. Installed ports kept."
exit 0
