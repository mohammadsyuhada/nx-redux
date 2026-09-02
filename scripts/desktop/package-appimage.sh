#!/bin/sh
# Build desktop binaries + cores and assemble the AppImage. Container-only:
# invoked via `docker compose run --rm appimage` (make package-linux).
set -eu
ROOT="$(pwd)"
# Prefer the tag the host Makefile already computed (BUILD_TAG, passed in as
# TAG via `docker compose run -e TAG=$(BUILD_TAG)`): this worktree's `.git`
# is a pointer file at an absolute *host* path (`gitdir: /Users/.../nx-redux/
# .git/worktrees/...`), which isn't reachable from inside the container (only
# this worktree's own directory is bind-mounted), so `git describe` run here
# always fails and falls back to "untagged". Keep that fallback for a
# standalone `docker compose run --rm appimage` invocation (no TAG set).
TAG="${TAG:-$(git -C "$ROOT" describe --tags --abbrev=0 2>/dev/null || echo untagged)}"
# Same container caveat as TAG: the host Makefile passes HASH=$(BUILD_HASH).
HASH="${HASH:-$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)}"
ARCH="${ARCH:-x86_64}"

# The bind-mounted repo keeps host ownership on its existing files; capture
# it now so everything this script creates as root can be handed back to
# the host user at the end (see step 6).
OWNER="$(stat -c '%u:%g' "$ROOT")"

# --- output/ collision guard ------------------------------------------
# workspace/desktop/cores/output/*.so is a *file-based* make target
# (output/$(1)_libretro.so: src/$(1)/.patched ...) shared with the macOS
# build (package-macos.sh): both OSes build PLATFORM=desktop into the same
# in-repo path. If we build here without moving the macOS-built (Mach-O)
# .so files out of the way first, our Linux .so overwrites them; then a
# *later* `make package-macos` on the host sees output/<core>_libretro.so
# already exists and (per the rule's file-timestamp semantics) skips
# rebuilding it, silently shipping a Linux ELF inside the next macOS zip.
# Stash any Mach-O artifacts aside before building, restore them after
# staging (step 4) copies what *this* build needs into the AppDir.
CORES_OUT="$ROOT/workspace/desktop/cores/output"
MACOS_STASH="$ROOT/workspace/desktop/cores/.output-macos-stash"
# NOTE: never `rm -rf` this dir unconditionally on entry — a prior run of
# this script that stashed the macOS .so files and then failed *before*
# reaching the restore step (step 4) leaves them sitting here; wiping the
# dir on the next attempt would destroy the only copy (learned the hard
# way: a first pass did exactly this and had to recover the originals from
# an already-packaged build/desktop-macos/NXRedux.app instead). Idempotent
# by construction: only ever moves a *currently-Mach-O* output/ file here.
mkdir -p "$MACOS_STASH"
for f in "$CORES_OUT"/*_libretro.so; do
	[ -f "$f" ] || continue
	if file "$f" | grep -q 'Mach-O'; then
		mv "$f" "$MACOS_STASH/"
	fi
done

# libchdr's own build dir (workspace/all/minarch/libchdr/build/desktop) has
# the same problem one level down: its make rule keys off an *order-only*
# directory prerequisite, so an existing (macOS, Mach-O) libchdr.so there
# reads as "up to date" and the recipe is skipped entirely — install then
# copies that Mach-O .so into this container's PREFIX_LOCAL, and the Linux
# minarch link fails on a wrong-format libchdr. minarch's actual consumed
# artifact for macOS lives in the *host's* /var/tmp/nxredux (not in this
# bind mount), so wiping this in-repo scratch dir doesn't touch anything a
# future `make package-macos` depends on — it just forces a clean rebuild
# here, same as it would on a fresh checkout.
rm -rf "$ROOT/workspace/all/minarch/libchdr/build/desktop"

# 1. build (native Linux: CROSS_COMPILE=/usr/bin/, PREFIX=/usr as Makefile.native's Linux branch does)
make -C workspace/desktop/libmsettings build CROSS_COMPILE=/usr/bin/ PREFIX=/usr PREFIX_LOCAL=/var/tmp/nxredux
make -C workspace/all/nextui PLATFORM=desktop CROSS_COMPILE=/usr/bin/ PREFIX=/usr PREFIX_LOCAL=/var/tmp/nxredux UNAME_S=Linux BUILD_TAG="$TAG"
make -C workspace/all/minarch PLATFORM=desktop CROSS_COMPILE=/usr/bin/ PREFIX=/usr PREFIX_LOCAL=/var/tmp/nxredux UNAME_S=Linux

# The 7 Tools paks' binaries (+ gametimectl's daemon). Same recipe as
# nextui/minarch above; ratools pulls in minarch's already-built libchdr/
# rcheevos via its own Makefile's $(PREFIX_LOCAL) deps, and gametime/
# gametimectl's libgametimedb.h dep triggers a `cd ../libgametimedb && make`
# that inherits PLATFORM/CROSS_COMPILE/PREFIX/PREFIX_LOCAL/UNAME_S via
# MAKEFLAGS (GNU make re-parses command-line var assignments from MAKEFLAGS
# in any child `make`, not just ones invoked through $(MAKE)).
for t in settings emu-options ratools scraper sync extras gametime gametimectl; do
	make -C "workspace/all/$t" PLATFORM=desktop CROSS_COMPILE=/usr/bin/ PREFIX=/usr PREFIX_LOCAL=/var/tmp/nxredux UNAME_S=Linux
done

# Core src/ dirs (workspace/desktop/cores/src/<core>) are shared with the
# macOS build the same way output/ is: cloned once, reused by both OSes
# (see the output/ collision guard above). Reuse of the *clone* is fine
# (same pin), but each core's own build leaves compiled .o/.so objects
# inside that same src/ tree, and its upstream Makefile keys off file
# mtimes, not architecture — a macOS-built (Mach-O) object looks "up to
# date" to a Linux `make` and gets hard-linked into a Linux .so, which
# fails at link time ("file not recognized: file format not recognized").
# `make clean-<core>` runs the core's own clean target without touching
# the clone itself, forcing a fresh Linux compile.
# Build the full CORES set (same set as package-macos.sh). `clean` first: the
# shared src/ trees may hold macOS (Mach-O) .o from a prior `make package-macos`
# that a Linux `make` would treat as up to date and hard-link into a broken .so.
make -C workspace/desktop/cores clean PLATFORM=desktop
make -C workspace/desktop/cores cores PLATFORM=desktop

# 2. AppDir
STAGE="$ROOT/build/desktop-linux"; APPDIR="$STAGE/AppDir"
rm -rf "$STAGE"; mkdir -p "$APPDIR/usr/system/bin" "$APPDIR/usr/system/cores" "$APPDIR/usr/lib"
cp -R skeleton/SYSTEM/desktop/. "$APPDIR/usr/system"
mkdir -p "$APPDIR/usr/system/shared" "$APPDIR/usr/system/res"
cp -R skeleton/SYSTEM/shared/. "$APPDIR/usr/system/shared"
cp -R skeleton/SYSTEM/res/.    "$APPDIR/usr/system/res"
# version.txt: same 3-line format the device Makefile ships (release name /
# hash / tag) — the Settings About page derives its version + release date
# from it and shows blanks when it's missing.
printf "%s\n%s\n%s\n" "NXRedux-$(TZ=GMT date +%Y%m%d)" "$HASH" "$TAG" > "$APPDIR/usr/system/version.txt"
cp -R skeleton/BASE            "$APPDIR/usr/base-skeleton"
# Flatten resolution-variant assets (overlays -> 768p, bg -> 1024) to match
# what the device build ships for Brick; desktop renders at 1024x768.
sh scripts/desktop/flatten-base.sh "$APPDIR/usr/base-skeleton"
cp workspace/all/nextui/build/desktop/nextui.elf   "$APPDIR/usr/system/bin/"
cp workspace/all/minarch/build/desktop/minarch.elf "$APPDIR/usr/system/bin/"
install -m 0755 scripts/desktop/check-update.sh "$APPDIR/usr/system/bin/"
install -m 0755 scripts/desktop/self-update.sh "$APPDIR/usr/system/bin/"
# glob (not a fixed list) so odd output names — vice_x64_libretro.so,
# stella2014_libretro.so, puae2021_libretro.so — come along automatically.
for so in workspace/desktop/cores/output/*_libretro.so; do cp "$so" "$APPDIR/usr/system/cores/"; done

# Tools paks: each pak's launch.sh cd's into its own dir and runs
# ./<binary>.elf (matches device layout, skeleton/SYSTEM/*/paks/Tools/*), so
# every tool binary lives pak-local, not in usr/system/bin. gametimectl.elf
# is the one exception: it's a stateless per-invocation CLI (list/start/
# resume/stop/stop_all against the play-time DB, no daemon/serve mode) that
# nextui.c/launcher.c invoke bare via PATH at ROM start/stop, matching
# device behavior exactly — so it needs to resolve off usr/system/bin
# (already on PATH, entry_export_env). A copy goes to both places.
cp workspace/all/settings/build/desktop/settings.elf       "$APPDIR/usr/system/paks/Tools/Settings.pak/"
cp workspace/all/emu-options/build/desktop/options.elf     "$APPDIR/usr/system/paks/Tools/Emulator Settings.pak/"
cp workspace/all/ratools/build/desktop/ratools.elf         "$APPDIR/usr/system/paks/Tools/RetroAchievements.pak/"
cp workspace/all/scraper/build/desktop/scraper.elf         "$APPDIR/usr/system/paks/Tools/Artwork Manager.pak/"
cp workspace/all/sync/build/desktop/sync.elf               "$APPDIR/usr/system/paks/Tools/Device Sync.pak/"
cp workspace/all/extras/build/desktop/extras.elf           "$APPDIR/usr/system/paks/Tools/Xtras.pak/"
cp workspace/all/gametime/build/desktop/gametime.elf       "$APPDIR/usr/system/paks/Tools/Game Tracker.pak/"
cp workspace/all/gametimectl/build/desktop/gametimectl.elf "$APPDIR/usr/system/paks/Tools/Game Tracker.pak/"
cp workspace/all/gametimectl/build/desktop/gametimectl.elf "$APPDIR/usr/system/bin/"
cp scripts/desktop/entry-common.sh "$APPDIR/usr/"
install -m 0755 scripts/desktop/AppRun.sh "$APPDIR/AppRun"
cp scripts/desktop/nxredux.desktop "$APPDIR/"
convert skeleton/SYSTEM/res/logo.png -resize 256x256 "$APPDIR/nxredux.png"

# 3. shared-lib closure (transparent ldd sweep; glibc family excluded)
EXCL='ld-linux|libc\.so|libm\.so|libpthread|libdl\.so|librt\.so|libresolv|libnsl'
for bin in "$APPDIR"/usr/system/bin/*.elf "$APPDIR"/usr/system/cores/*.so "$APPDIR"/usr/system/paks/Tools/*/*.elf; do
	ldd "$bin" 2>/dev/null | awk '/=> \//{print $3}' | grep -Ev "$EXCL" | while read -r lib; do
		cp -n "$lib" "$APPDIR/usr/lib/" || true
	done
done

# libmsettings.so, libchdr.so.0, and libgametimedb.so are our own in-tree
# libs, linked with a bare (path-less) name and no ldconfig/system entry, so
# a plain `ldd` on the just-built elfs reports them as "not found" (it can
# only resolve what the *current* environment's linker would find; usr/lib
# doesn't exist as a search path until this script populates it). The awk
# filter above only matches resolved ("=> /...") lines, so these are
# silently skipped by the sweep — copy them in explicitly. libchdr's built
# filename (libchdr.so) doesn't match the SONAME minarch actually links
# against (libchdr.so.0, confirmed via readelf -d); install it under that
# name so the AppRun-set LD_LIBRARY_PATH resolves it like every other
# bundled lib. libgametimedb.so (gametime.elf/gametimectl.elf) has no such
# SONAME mismatch (readelf -d confirms the DT_NEEDED entry is the plain
# built filename), so it needs no rename.
cp workspace/desktop/libmsettings/libmsettings.so "$APPDIR/usr/lib/"
cp workspace/all/minarch/libchdr/build/desktop/libchdr.so "$APPDIR/usr/lib/libchdr.so.0"
cp workspace/all/libgametimedb/build/desktop/libgametimedb.so "$APPDIR/usr/lib/"

# 4. restore macOS core artifacts (see output/ collision guard above) so the
# working tree stays coherent for a later `make package-macos`. The Linux
# .so files this AppImage needs are already staged in the AppDir; output/
# itself goes back to whatever it held before this script ran (untouched
# if it was empty, i.e. no prior macOS build).
rm -f "$CORES_OUT"/*_libretro.so
mv "$MACOS_STASH"/*.so "$CORES_OUT/" 2>/dev/null || true
rm -rf "$MACOS_STASH"

# 5. pack (no FUSE inside docker)
mkdir -p releases
OUT="releases/NXRedux-$TAG-$ARCH.AppImage"
rm -f "$OUT"
ARCH="$ARCH" appimagetool --appimage-extract-and-run "$APPDIR" "$OUT"
echo "packaged: $OUT"

# 6. hand root-owned build/releases output back to the host user (bind
# mount has no uid mapping, so anything created above is root:root on the
# host otherwise)
chown -R "$OWNER" "$ROOT/build" "$ROOT/releases"
