#!/bin/sh

SDCARD_PATH=/mnt/SDCARD

# --------------------------------------
# clean shipped-name paks out of /Emus and /Tools (moved into .system
# 2026-07-31; removal of this hook is tracked in DEV_TODO.md);
# must never fail the update
sh ${SDCARD_PATH}/.system/shared/bin/migrate-paks.sh tg5050 || true

# --------------------------------------
# migration code here
# --------------------------------------
