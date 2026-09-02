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

# Per-OS build subdir (see workspace/all/*/Makefile BUILD_SUBDIR): this
# container's app binaries land in build/desktop-linux-<arch>, fully apart
# from the host's build/desktop-macos-<arch> — the two packaging flows can
# run concurrently. PREFIX_LOCAL artifacts (librcheevos.a, libchdr.so,
# libmsettings.so, gametimedb.h) are per-OS by nature: /var/tmp here is
# container-local, not the bind mount.
SUBDIR="desktop-linux-$(uname -m)"

# 1. build (native Linux: CROSS_COMPILE=/usr/bin/, PREFIX=/usr as Makefile.native's Linux branch does)
make -C workspace/desktop/libmsettings build CROSS_COMPILE=/usr/bin/ PREFIX=/usr PREFIX_LOCAL=/var/tmp/nxredux BUILD_SUBDIR="$SUBDIR"
make -C workspace/all/nextui PLATFORM=desktop CROSS_COMPILE=/usr/bin/ PREFIX=/usr PREFIX_LOCAL=/var/tmp/nxredux UNAME_S=Linux BUILD_TAG="$TAG" BUILD_SUBDIR="$SUBDIR"
make -C workspace/all/minarch PLATFORM=desktop CROSS_COMPILE=/usr/bin/ PREFIX=/usr PREFIX_LOCAL=/var/tmp/nxredux UNAME_S=Linux BUILD_SUBDIR="$SUBDIR"

# The 7 Tools paks' binaries (+ gametimectl's daemon). Same recipe as
# nextui/minarch above; ratools pulls in minarch's already-built libchdr/
# rcheevos via its own Makefile's $(PREFIX_LOCAL) deps, and gametime/
# gametimectl's libgametimedb.h dep triggers a `cd ../libgametimedb && make`
# that inherits PLATFORM/CROSS_COMPILE/PREFIX/PREFIX_LOCAL/UNAME_S via
# MAKEFLAGS (GNU make re-parses command-line var assignments from MAKEFLAGS
# in any child `make`, not just ones invoked through $(MAKE)).
for t in settings emu-options ratools scraper sync extras gametime gametimectl; do
	make -C "workspace/all/$t" PLATFORM=desktop CROSS_COMPILE=/usr/bin/ PREFIX=/usr PREFIX_LOCAL=/var/tmp/nxredux UNAME_S=Linux BUILD_SUBDIR="$SUBDIR"
done

# Build the full CORES set (same set as package-macos.sh). The cores Makefile
# derives a host triple (linux-x86_64 here) and keeps this OS's clones,
# objects, and output under src/<triple> and output/<triple>, fully separate
# from the host's macOS trees — no cleaning or stashing needed.
make -C workspace/desktop/cores cores PLATFORM=desktop

# 2. AppDir
STAGE="$ROOT/build/desktop-linux"; APPDIR="$STAGE/AppDir"
rm -rf "$STAGE"; mkdir -p "$APPDIR/usr/system/bin" "$APPDIR/usr/system/cores" "$APPDIR/usr/lib"
cp -R skeleton/SYSTEM/desktop/. "$APPDIR/usr/system"
mkdir -p "$APPDIR/usr/system/shared" "$APPDIR/usr/system/res"
cp -R skeleton/SYSTEM/shared/. "$APPDIR/usr/system/shared"
# The shared skeleton vendors DEVICE (aarch64) binaries: dead weight on
# x86_64, and their presence defeats vendored-tool existence checks
# (access(X_OK) passes, exec fails — this is how Device Sync hung on
# 'waiting'). Strip them and ship the distro's rsync instead (Device Sync
# hosts an rsync daemon on both ends; its libs ride the ldd sweep below).
for dead in 7zzs.aarch64 ffplay rsync wget taskset yt-dlp yt-dlp.old yt-dlp_version.txt; do
	rm -f "$APPDIR/usr/system/shared/bin/$dead"
done
command -v rsync >/dev/null 2>&1 && command -v 7zz >/dev/null 2>&1 || { apt-get update -qq && apt-get install -y -qq rsync 7zip; }
cp "$(command -v rsync)" "$APPDIR/usr/system/shared/bin/rsync"
# 7zz: minarch's .7z ROM extraction (ma_game.c); libs ride the ldd sweep.
cp "$(command -v 7zz)" "$APPDIR/usr/system/shared/bin/7zz"
cp -R skeleton/SYSTEM/res/.    "$APPDIR/usr/system/res"
# version.txt: same 3-line format the device Makefile ships (release name /
# hash / tag) — the Settings About page derives its version + release date
# from it and shows blanks when it's missing.
printf "%s\n%s\n%s\n" "NXRedux-$(TZ=GMT date +%Y%m%d)" "$HASH" "$TAG" > "$APPDIR/usr/system/version.txt"
cp -R skeleton/BASE            "$APPDIR/usr/base-skeleton"
# Flatten resolution-variant assets (overlays -> 768p, bg -> 1024) to match
# what the device build ships for Brick; desktop renders at 1024x768.
sh scripts/desktop/flatten-base.sh "$APPDIR/usr/base-skeleton"
cp workspace/all/nextui/build/$SUBDIR/nextui.elf   "$APPDIR/usr/system/bin/"
cp workspace/all/minarch/build/$SUBDIR/minarch.elf "$APPDIR/usr/system/bin/"
install -m 0755 scripts/desktop/check-update.sh "$APPDIR/usr/system/bin/"
install -m 0755 scripts/desktop/self-update.sh "$APPDIR/usr/system/bin/"
# glob (not a fixed list) so odd output names — vice_x64_libretro.so,
# stella2014_libretro.so, puae2021_libretro.so — come along automatically.
for so in workspace/desktop/cores/output/linux-x86_64/*_libretro.so; do cp "$so" "$APPDIR/usr/system/cores/"; done

# Tools paks: each pak's launch.sh cd's into its own dir and runs
# ./<binary>.elf (matches device layout, skeleton/SYSTEM/*/paks/Tools/*), so
# every tool binary lives pak-local, not in usr/system/bin. gametimectl.elf
# is the one exception: it's a stateless per-invocation CLI (list/start/
# resume/stop/stop_all against the play-time DB, no daemon/serve mode) that
# nextui.c/launcher.c invoke bare via PATH at ROM start/stop, matching
# device behavior exactly — so it needs to resolve off usr/system/bin
# (already on PATH, entry_export_env). A copy goes to both places.
cp workspace/all/settings/build/$SUBDIR/settings.elf       "$APPDIR/usr/system/paks/Tools/Settings.pak/"
cp workspace/all/emu-options/build/$SUBDIR/options.elf     "$APPDIR/usr/system/paks/Tools/Emulator Settings.pak/"
cp workspace/all/ratools/build/$SUBDIR/ratools.elf         "$APPDIR/usr/system/paks/Tools/RetroAchievements.pak/"
cp workspace/all/scraper/build/$SUBDIR/scraper.elf         "$APPDIR/usr/system/paks/Tools/Artwork Manager.pak/"
cp workspace/all/sync/build/$SUBDIR/sync.elf               "$APPDIR/usr/system/paks/Tools/Device Sync.pak/"
cp workspace/all/extras/build/$SUBDIR/extras.elf           "$APPDIR/usr/system/paks/Tools/Xtras.pak/"
cp workspace/all/gametime/build/$SUBDIR/gametime.elf       "$APPDIR/usr/system/paks/Tools/Game Tracker.pak/"
cp workspace/all/gametimectl/build/$SUBDIR/gametimectl.elf "$APPDIR/usr/system/paks/Tools/Game Tracker.pak/"
cp workspace/all/gametimectl/build/$SUBDIR/gametimectl.elf "$APPDIR/usr/system/bin/"
cp scripts/desktop/entry-common.sh "$APPDIR/usr/"
install -m 0755 scripts/desktop/AppRun.sh "$APPDIR/AppRun"
cp scripts/desktop/nxredux.desktop "$APPDIR/"
convert skeleton/SYSTEM/res/logo.png -resize 256x256 "$APPDIR/nxredux.png"

# 3. shared-lib closure (transparent ldd sweep; glibc family excluded)
EXCL='ld-linux|libc\.so|libm\.so|libpthread|libdl\.so|librt\.so|libresolv|libnsl'
for bin in "$APPDIR"/usr/system/bin/*.elf "$APPDIR"/usr/system/cores/*.so "$APPDIR"/usr/system/paks/Tools/*/*.elf "$APPDIR"/usr/system/shared/bin/rsync "$APPDIR"/usr/system/shared/bin/7zz; do
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
cp workspace/desktop/libmsettings/build/$SUBDIR/libmsettings.so "$APPDIR/usr/lib/"
cp workspace/all/minarch/libchdr/build/$SUBDIR/libchdr.so "$APPDIR/usr/lib/libchdr.so.0"
cp workspace/all/libgametimedb/build/$SUBDIR/libgametimedb.so "$APPDIR/usr/lib/"

# 4. pack (no FUSE inside docker)
mkdir -p releases
OUT="releases/NXRedux-$TAG-$ARCH.AppImage"
rm -f "$OUT"
ARCH="$ARCH" appimagetool --appimage-extract-and-run "$APPDIR" "$OUT"
echo "packaged: $OUT"

# 5. hand root-owned build/releases output back to the host user (bind
# mount has no uid mapping, so anything created above is root:root on the
# host otherwise). The cores trees too: CI caches src/output between runs
# (actions/cache in release.yaml) and its post-job save runs as the runner
# user, which can't archive root-owned files it can't read.
chown -R "$OWNER" "$ROOT/build" "$ROOT/releases" "$ROOT/workspace/desktop/cores"
