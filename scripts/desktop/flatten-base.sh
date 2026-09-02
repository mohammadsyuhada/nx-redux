#!/bin/sh
# Flatten skeleton/BASE resolution-variant assets down to the desktop
# resolution, mirroring the per-device resolution resolution the top-level
# Makefile performs for real hardware (see its "resolving overlays / bg
# images" step in the package target). Desktop renders at 1024x768 == Brick,
# so it ships the Brick set: 768p overlays and the 1024 background variant.
#
# Both desktop packagers (package-macos.sh, package-appimage.sh) call this on
# their bundled base-skeleton so a freshly seeded ~/NXRedux card is already
# flat: the in-game overlay picker lists Overlays/<tag>/*.png directly (no res
# subdir, which is all minarch's Overlays/<tag> scan understands), and each
# folder's .media/bg.png is the correct size.
set -eu

BASE="${1:?usage: flatten-base.sh <base-skeleton-dir>}"
OVERLAY_RES=768p
BG_RES=1024

# --- Overlays: Overlays/<tag>/<res>/*.png -> Overlays/<tag>/*.png -----------
# then remove BOTH res subdirs so only the flattened set ships.
if [ -d "$BASE/Overlays" ]; then
	for tag_dir in "$BASE/Overlays"/*/; do
		[ -d "$tag_dir" ] || continue
		if [ -d "$tag_dir$OVERLAY_RES" ]; then
			cp -f "$tag_dir$OVERLAY_RES"/*.png "$tag_dir" 2>/dev/null || true
		fi
		rm -rf "$tag_dir"720p "$tag_dir"768p
	done
fi

# --- Backgrounds: .media/bg-<res>.png -> .media/bg.png ----------------------
# then drop every bg-*.png variant. Covers every tree that ships per-res art.
for root in Roms Collections Favorites "Recently Played" Tools; do
	[ -d "$BASE/$root" ] || continue
	find "$BASE/$root" -type f -path '*/.media/bg-'"$BG_RES"'.png' 2>/dev/null \
	| while IFS= read -r f; do
		cp -f "$f" "$(dirname "$f")/bg.png"
	done
	find "$BASE/$root" -type f -path '*/.media/bg-*.png' -delete 2>/dev/null || true
done
