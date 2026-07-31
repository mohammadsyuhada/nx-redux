#!/bin/sh
# legacy-boot compat shim: the old .tmp_update launches MinUI here on the
# first update after the .system flatten.
exec /mnt/SDCARD/.system/paks/MinUI.pak/launch.sh "$@"
