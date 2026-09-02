#!/bin/sh
# No-sudo macOS toolchain shim: the build invokes $(CROSS_COMPILE)gcc and
# Homebrew installs only versioned names (gcc-16). NEVER symlink into
# /usr/local/bin (other tools live there).
set -eu
SHIM_DIR=/var/tmp/nxredux/bin
GCC_BIN_DIR="$(brew --prefix gcc)/bin"
GCC_BIN="$(ls "$GCC_BIN_DIR"/gcc-[0-9]* 2>/dev/null | sort -V | tail -1)"
[ -x "$GCC_BIN" ] || { echo "error: brew gcc not found (brew install gcc)" >&2; exit 1; }
mkdir -p "$SHIM_DIR"
ln -sf "$GCC_BIN" "$SHIM_DIR/gcc"
echo "shim: $SHIM_DIR/gcc -> $GCC_BIN"
