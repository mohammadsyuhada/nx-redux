#!/bin/sh
# entry-common.sh unit test: fake bundle + temp HOME, assert seeding + env.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
# fake system tree + base skeleton
mkdir -p "$TMP/sys/bin" "$TMP/base/Roms/Game Boy (GB)" "$TMP/base/Bios"
touch "$TMP/base/README.txt"
export HOME="$TMP/home"; mkdir -p "$HOME"
unset NXREDUX_SDCARD NXREDUX_SYSTEM_ROOT || true

. "$HERE/../entry-common.sh"
entry_resolve_roots "$TMP/sys" "$TMP/base"
entry_seed_card
entry_export_env

[ "$CARD" = "$HOME/NXRedux" ] || { echo "FAIL: CARD=$CARD"; exit 1; }
[ -d "$CARD/Roms/Game Boy (GB)" ] || { echo "FAIL: not seeded"; exit 1; }
[ -d "$CARD/.userdata/desktop/logs" ] || { echo "FAIL: no logs dir"; exit 1; }
[ -d "$CARD/.shadercache" ] || { echo "FAIL: no shadercache"; exit 1; }
[ "$NXREDUX_SYSTEM_ROOT" = "$TMP/sys" ] || { echo "FAIL: SYS env"; exit 1; }
[ "$CORES_PATH" = "$TMP/sys/cores" ] || { echo "FAIL: CORES_PATH"; exit 1; }
[ "$USERDATA_PATH" = "$CARD/.userdata/desktop" ] || { echo "FAIL: USERDATA_PATH"; exit 1; }
[ "$DEVICE" = "desktop" ] || { echo "FAIL: DEVICE"; exit 1; }
# idempotence: second seed must not touch an existing card
touch "$CARD/Roms/marker"
entry_seed_card
[ -f "$CARD/Roms/marker" ] || { echo "FAIL: reseeded over existing card"; exit 1; }
echo "test_entry_common: OK"
