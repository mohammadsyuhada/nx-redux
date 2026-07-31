#!/bin/sh
# legacy-boot compat shim (first update after the .system flatten): the old
# on-card .tmp_update invokes install.sh here. Flag the legacy boot so the
# migration keeps these shims alive for this one run, then run the real one.
touch /tmp/nx_legacy_boot
exec /mnt/SDCARD/.system/bin/install.sh
