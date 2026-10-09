#!/usr/bin/env bash
# The n64-standalone-cleanup block of update.sh (both platforms), run alone.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
FAIL=0; fail() { echo "FAIL: $*"; FAIL=1; }
for PLAT in tg5040 tg5050; do
	U="workspace/$PLAT/install/update.sh"
	sed -n '/--- n64-standalone-cleanup-begin/,/--- n64-standalone-cleanup-end/p' "$U" > "$TMP/block.sh"
	[ -s "$TMP/block.sh" ] || { fail "$PLAT: block missing"; continue; }
	SD="$TMP/sd"; rm -rf "$SD"; mkdir -p "$SD/Emus/shared/mupen64plus" "$SD/Roms/Nintendo 64 (N64)/.hires_texture/MK64" "$SD/.userdata/shared/N64-mupen64plus/data/mupen64plus/save" "$TMP/udisk"
	echo x > "$SD/Emus/shared/mupen64plus/overlay_settings.json"
	echo tex > "$SD/Roms/Nintendo 64 (N64)/.hires_texture/MK64/a.png"
	echo save > "$SD/.userdata/shared/N64-mupen64plus/data/mupen64plus/save/MK-3A67D998.eep"
	echo swap > "$TMP/udisk/n64_swap"
	SDCARD_PATH="$SD" NX_UDISK="$TMP/udisk" sh "$TMP/block.sh"
	[ ! -e "$SD/Emus/shared/mupen64plus" ] || fail "$PLAT: standalone data removed"
	[ -f "$TMP/udisk/n64_swap" ] || fail "$PLAT: swap file kept (N64.pak still uses it)"
	[ -f "$SD/Roms/Nintendo 64 (N64)/.hires_texture/MK64/a.png" ] || fail "$PLAT: hi-res packs stay where they are"
	[ -f "$SD/.userdata/shared/N64-mupen64plus/data/mupen64plus/save/MK-3A67D998.eep" ] || fail "$PLAT: saves untouched"
done
[ "$FAIL" = 0 ] && echo "ALL PASS" || exit 1
