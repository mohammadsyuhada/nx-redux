#!/bin/bash
set -e

WORK_DIR="/Users/mohammadsyuhada/Work/Personal/NextUI-Redux/workspace/tg5050/other/sdl2-drastic"
SDL_DIR="$WORK_DIR/SDL_drastic"
TMPDIR="/tmp/sdl2-drastic-baseline-tg5050"
PATCH_0006="$WORK_DIR/0006-add-hook-for-drastic.patch"
OUTPUT="$WORK_DIR/fix-tg5050.patch"

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
# The diff will have the full /tmp/... and /Users/... paths
# We need to replace them with a/ and b/ relative paths
sed -i '' "s|$TMPDIR/|a/|g" "$OUTPUT"
sed -i '' "s|$SDL_DIR/|b/|g" "$OUTPUT"

echo "Paths fixed."
wc -l "$OUTPUT"

# Clean up worktree
echo "Cleaning up worktree..."
git -C "$SDL_DIR" worktree remove "$TMPDIR" 2>/dev/null || true

echo "Done!"
