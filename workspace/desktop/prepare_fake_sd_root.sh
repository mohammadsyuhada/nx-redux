#!/bin/bash

# Prepares a faux SD card structure for debugging on desktop
# macOS: /var/tmp/nxredux/sdcard
# Linux: /var/tmp/nxredux/sdcard

# 1. Check if it already exists, we will call this from Makefile. If already prepared, bail and do nothing
# 2. Copy folder structure from skeleton/(BASE,SYSTEM) into the folder
set -euo pipefail

TARGET="/var/tmp/nxredux/sdcard"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKELETON_DIR="$(cd "$SCRIPT_DIR/../../skeleton" && pwd)"

# Bail if already prepared
if [ -d "$TARGET" ]; then
    echo "SD root already exists at: $TARGET"
    exit 0
fi

# Ensure skeleton exists
if [ ! -d "$SKELETON_DIR" ]; then
    echo "Skeleton directory not found: $SKELETON_DIR" >&2
    exit 1
fi

# Create target
mkdir -p "$TARGET"

PLATFORM="desktop"

# Copy a directory's contents into a destination, preferring rsync.
copy_tree() {
    src="$1"; dst="$2"
    [ -d "$src" ] || return 0
    mkdir -p "$dst"
    if command -v rsync >/dev/null 2>&1; then
        rsync -a "$src"/ "$dst"/
    else
        cp -R "$src"/. "$dst"/
    fi
}

# Copy structure in specific order: BASE, then SYSTEM.
# BASE lands at the SD root as-is. SYSTEM is assembled in the FLATTENED
# layout the packaged card ships (see Makefile "assembling .system"): the
# desktop platform subtree's contents live directly under .system/ (bin, cores,
# lib, paks, ...), with shared/ and res/ alongside — no .system/<plat>/ level.
copy_tree "$SKELETON_DIR/BASE"   "$TARGET"
copy_tree "$SKELETON_DIR/SYSTEM/$PLATFORM" "$TARGET/.system"
copy_tree "$SKELETON_DIR/SYSTEM/shared"    "$TARGET/.system/shared"
copy_tree "$SKELETON_DIR/SYSTEM/res"       "$TARGET/.system/res"

echo "Prepared faux SD root at: $TARGET"
