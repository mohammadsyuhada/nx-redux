#!/bin/sh
# Xtras catalog: RHH Ports uninstall. Takes the catalog out of PortMaster
# (its source file and cached images), removes gmtoolkit and drops the
# $XTRAS_STATE_DIR/portmaster-rhh.version marker. Ports already installed
# from it stay, with their saves; remove those from PortMaster.
# Self-contained; no network.
set -u

: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"
PM_DIR="$SDCARD_PATH/Emus/shared/PortMaster"

echo "@50 Removing the RHH catalog from PortMaster..."
rm -f "$PM_DIR/config/030_rhh.source.json"
rm -rf "$PM_DIR/config/images_rhh"
rm -f "$PM_DIR/gmtoolkit.aarch64" "$PM_DIR/gmtoolkit.aarch64.nxtmp" "$PM_DIR/gmtoolkit.LICENSE.txt"
rm -f "$XTRAS_STATE_DIR/portmaster-rhh.version"

echo "@100 Done"
echo "Removed. Installed ports kept."
exit 0
