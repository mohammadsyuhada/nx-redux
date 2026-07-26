#!/bin/bash
set -e

# Regenerates fix-<platform>.patch: the diff between a pristine SDL_drastic
# baseline (base commit eb2e00f + the shared 0006 hook patch) and the current
# per-platform working tree at workspace/<platform>/other/sdl2-drastic/SDL_drastic.
# The regenerated fix-<platform>.patch is written next to this script.
#
# Usage: ./gen-patch.sh <tg5040|tg5050>

PLATFORM="$1"
case "$PLATFORM" in
	tg5040|tg5050) ;;
	*) echo "usage: $0 <tg5040|tg5050>" >&2; exit 1 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="$SCRIPT_DIR/../../../$PLATFORM/other/sdl2-drastic"
SDL_DIR="$WORK_DIR/SDL_drastic"
TMPDIR="/tmp/sdl2-drastic-baseline-$PLATFORM"
PATCH_0006="$SCRIPT_DIR/0006-add-hook-for-drastic.patch"
OUTPUT="$SCRIPT_DIR/fix-$PLATFORM.patch"

# Clean up any previous worktree
git -C "$SDL_DIR" worktree remove "$TMPDIR" 2>/dev/null || true
rm -rf "$TMPDIR"

# Create worktree at the base commit
echo "Creating worktree at eb2e00f..."
git -C "$SDL_DIR" worktree add --detach "$TMPDIR" eb2e00f

# Apply the 0006 patch to the worktree
echo "Applying 0006-add-hook-for-drastic.patch..."
cd "$TMPDIR"
patch -p1 < "$PATCH_0006"

echo "Baseline ready. Generating diff..."

# Generate diff: baseline (0006 applied) vs current working tree
# diff returns exit code 1 when files differ, which is expected
diff -ruN \
  --exclude='.git' \
  --exclude='build' \
  --exclude='*.o' \
  --exclude='*.a' \
  --exclude='*.la' \
  --exclude='*.lo' \
  --exclude='*.orig' \
  --exclude='Makefile' \
  --exclude='Makefile.rules' \
  --exclude='config.log' \
  --exclude='config.status' \
  --exclude='libtool' \
  --exclude='sdl2-config' \
  --exclude='sdl2-config.cmake' \
  --exclude='sdl2-config-version.cmake' \
  --exclude='sdl2.pc' \
  --exclude='SDL2.spec' \
  --exclude='gen' \
  --exclude='autom4te.cache' \
  --exclude='SDL_config.h' \
  --exclude='SDL_revision.h' \
  "$TMPDIR/" "$SDL_DIR/" > "$OUTPUT" || true

echo "Patch generated at $OUTPUT"
wc -l "$OUTPUT"

# Fix paths in the patch file to use a/ and b/ prefixes
# The diff will have the full /tmp/... and absolute workspace paths
sed -i '' "s|$TMPDIR/|a/|g" "$OUTPUT"
sed -i '' "s|$SDL_DIR/|b/|g" "$OUTPUT"

echo "Paths fixed."
wc -l "$OUTPUT"

# Clean up worktree
echo "Cleaning up worktree..."
git -C "$SDL_DIR" worktree remove "$TMPDIR" 2>/dev/null || true

echo "Done!"
