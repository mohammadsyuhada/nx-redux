#!/bin/sh
# Assemble NXRedux.app from built desktop artifacts. Run via: make package-macos
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TAG="${TAG:-$(cd "$ROOT" && git describe --tags --abbrev=0 2>/dev/null || echo untagged)}"
HASH="$(cd "$ROOT" && git rev-parse --short HEAD)"
STAGE="$ROOT/build/desktop-macos"
APP="$STAGE/NXRedux.app"
SYS="$APP/Contents/Resources/system"

command -v dylibbundler >/dev/null || { echo "error: brew install dylibbundler" >&2; exit 1; }

rm -rf "$STAGE"; mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"

# .system tree, assembled exactly like prepare_fake_sd_root.sh flattens it
cp -R "$ROOT/skeleton/SYSTEM/desktop/." "$SYS"
mkdir -p "$SYS/shared" "$SYS/res" "$SYS/bin" "$SYS/cores"
cp -R "$ROOT/skeleton/SYSTEM/shared/." "$SYS/shared"
cp -R "$ROOT/skeleton/SYSTEM/res/." "$SYS/res"

# version.txt: same 3-line format the device Makefile ships (release name /
# hash / tag) — the Settings About page derives its version + release date
# from it and shows blanks when it's missing.
printf "%s\n%s\n%s\n" "NXRedux-$(TZ=GMT date +%Y%m%d)" "$HASH" "$TAG" > "$SYS/version.txt"

# binaries + cores
cp "$ROOT/workspace/all/nextui/build/desktop/nextui.elf" "$SYS/bin/"
cp "$ROOT/workspace/all/minarch/build/desktop/minarch.elf" "$SYS/bin/"
install -m 0755 "$ROOT/scripts/desktop/check-update.sh" "$SYS/bin/"
install -m 0755 "$ROOT/scripts/desktop/self-update.sh" "$SYS/bin/"
# Copy every built core. Glob (not a fixed list) so odd output names —
# vice_x64_libretro.so, stella2014_libretro.so, puae2021_libretro.so — come
# along automatically; the desktop cores Makefile's CORES list is what the
# build step just produced in output/. Each Emus/<TAG>.pak/launch.sh loads its
# core from $CORES_PATH by name.
for so in "$ROOT/workspace/desktop/cores/output/"*_libretro.so; do
	[ -f "$so" ] && cp "$so" "$SYS/cores/"
done

# Tools paks: each pak's launch.sh cd's into its own dir and runs
# ./<binary>.elf (matches device layout, skeleton/SYSTEM/*/paks/Tools/*), so
# every tool binary lives pak-local, not in $SYS/bin. gametimectl.elf is the
# one exception: it's a stateless per-invocation CLI (list/start/resume/
# stop/stop_all against the play-time DB, no daemon/serve mode — confirmed
# via gametimectl.c's main(): argc<=1 just prints usage and exits 0) that
# nextui.c/launcher.c invoke bare via PATH at ROM start/stop (eg.
# system("gametimectl.elf stop_all &")), matching device behavior exactly —
# so it needs to resolve off $SYS/bin (already on PATH, entry_export_env),
# same as on device. A copy goes to both places.
SETTINGS_ELF="$SYS/paks/Tools/Settings.pak/settings.elf"
OPTIONS_ELF="$SYS/paks/Tools/Emulator Settings.pak/options.elf"
RATOOLS_ELF="$SYS/paks/Tools/RetroAchievements.pak/ratools.elf"
SCRAPER_ELF="$SYS/paks/Tools/Artwork Manager.pak/scraper.elf"
SYNC_ELF="$SYS/paks/Tools/Device Sync.pak/sync.elf"
EXTRAS_ELF="$SYS/paks/Tools/Xtras.pak/extras.elf"
GAMETIME_ELF="$SYS/paks/Tools/Game Tracker.pak/gametime.elf"
GAMETIMECTL_PAK_ELF="$SYS/paks/Tools/Game Tracker.pak/gametimectl.elf"
GAMETIMECTL_BIN_ELF="$SYS/bin/gametimectl.elf"
cp "$ROOT/workspace/all/settings/build/desktop/settings.elf" "$SETTINGS_ELF"
cp "$ROOT/workspace/all/emu-options/build/desktop/options.elf" "$OPTIONS_ELF"
cp "$ROOT/workspace/all/ratools/build/desktop/ratools.elf" "$RATOOLS_ELF"
cp "$ROOT/workspace/all/scraper/build/desktop/scraper.elf" "$SCRAPER_ELF"
cp "$ROOT/workspace/all/sync/build/desktop/sync.elf" "$SYNC_ELF"
cp "$ROOT/workspace/all/extras/build/desktop/extras.elf" "$EXTRAS_ELF"
cp "$ROOT/workspace/all/gametime/build/desktop/gametime.elf" "$GAMETIME_ELF"
cp "$ROOT/workspace/all/gametimectl/build/desktop/gametimectl.elf" "$GAMETIMECTL_PAK_ELF"
cp "$ROOT/workspace/all/gametimectl/build/desktop/gametimectl.elf" "$GAMETIMECTL_BIN_ELF"

# libmsettings.so is linked in with a bare (path-less) install name — dylibbundler
# can locate it via -s but chokes rewriting its rpath, so give the reference an
# absolute path before bundling. Work on a disposable copy, not the build
# output under workspace/, so its rpath-strip/re-sign below (and dylibbundler's
# own re-sign) never touch a file other dev workflows might load directly.
mkdir -p "$STAGE/tmp"
LIBMSETTINGS="$STAGE/tmp/libmsettings.so"
cp "$ROOT/workspace/desktop/libmsettings/libmsettings.so" "$LIBMSETTINGS"
for exe in "$SYS/bin/nextui.elf" "$SYS/bin/minarch.elf" \
	"$SETTINGS_ELF" "$OPTIONS_ELF" "$RATOOLS_ELF" "$SCRAPER_ELF" "$SYNC_ELF" "$EXTRAS_ELF" \
	"$GAMETIME_ELF" "$GAMETIMECTL_PAK_ELF" "$GAMETIMECTL_BIN_ELF"; do
	install_name_tool -change libmsettings.so "$LIBMSETTINGS" "$exe"
done

# gametime.elf/gametimectl.elf also link libgametimedb.so, with an even
# odder install name than libmsettings.so's bare one: the *relative build
# path* used at link time ("build/desktop/libgametimedb.so" — see
# workspace/all/libgametimedb/Makefile's PRODUCT, no -install_name passed).
# Same dylibbundler failure mode (it locates the file fine via -s by
# basename, then chokes trying to otool -L the literal unresolved path
# string when it recurses into that dependency's own rpaths) — same fix.
LIBGAMETIMEDB="$STAGE/tmp/libgametimedb.so"
cp "$ROOT/workspace/all/libgametimedb/build/desktop/libgametimedb.so" "$LIBGAMETIMEDB"
for exe in "$GAMETIME_ELF" "$GAMETIMECTL_PAK_ELF" "$GAMETIMECTL_BIN_ELF"; do
	install_name_tool -change build/desktop/libgametimedb.so "$LIBGAMETIMEDB" "$exe"
done

# gcc auto-embeds several LC_RPATH entries (its own toolchain lib dirs, plus a
# bare @loader_path/$ORIGIN default) in every binary it links — nextui.elf,
# minarch.elf, and libmsettings.so alike. dylibbundler individually rewrites
# each to the same bundled path below, producing duplicate LC_RPATH commands
# dyld refuses to load ("duplicate LC_RPATH ... in <binary>"). Strip them
# first; dylibbundler adds back exactly one bundled rpath for whichever
# binary actually needs it (minarch/ratools, for @rpath/libchdr.0.so).
for bin in "$SYS/bin/nextui.elf" "$SYS/bin/minarch.elf" "$LIBMSETTINGS" \
	"$SETTINGS_ELF" "$OPTIONS_ELF" "$RATOOLS_ELF" "$SCRAPER_ELF" "$SYNC_ELF" "$EXTRAS_ELF" \
	"$GAMETIME_ELF" "$GAMETIMECTL_PAK_ELF" "$GAMETIMECTL_BIN_ELF" "$LIBGAMETIMEDB"; do
	otool -l "$bin" | awk '
		/LC_RPATH/ { f=1; next }
		f && /path/ { sub(/^ *path /, ""); sub(/ \(offset.*/, ""); print; f=0 }
	' | while IFS= read -r rp; do
		install_name_tool -delete_rpath "$rp" "$bin"
	done
done

# base skeleton + entry + metadata
cp -R "$ROOT/skeleton/BASE" "$APP/Contents/Resources/base-skeleton"
# Flatten resolution-variant assets (overlays -> 768p, bg -> 1024) to match
# what the device build ships for Brick; desktop renders at 1024x768.
sh "$ROOT/scripts/desktop/flatten-base.sh" "$APP/Contents/Resources/base-skeleton"
cp "$ROOT/scripts/desktop/entry-common.sh" "$APP/Contents/Resources/"
install -m 0755 "$ROOT/scripts/desktop/macos-entry.sh" "$APP/Contents/MacOS/NXRedux"
sed -e "s/@VERSION@/$TAG/" -e "s/@HASH@/$HASH/" \
	"$ROOT/scripts/desktop/Info.plist.in" > "$APP/Contents/Info.plist"

# icon: 1024px logo -> icns (stock tools only)
ICONSRC="$ROOT/skeleton/SYSTEM/res/logo.png"
ICONSET="$STAGE/icon.iconset"; mkdir -p "$ICONSET"
for s in 16 32 64 128 256 512; do
	sips -z $s $s "$ICONSRC" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
	sips -z $((s*2)) $((s*2)) "$ICONSRC" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/icon.icns"

# bundle dylib closure; bin/ is 3 levels below Contents/
for exe in "$SYS/bin/nextui.elf" "$SYS/bin/minarch.elf" "$GAMETIMECTL_BIN_ELF"; do
	dylibbundler -of -cd -b -x "$exe" \
		-d "$APP/Contents/Frameworks" -p '@executable_path/../../../Frameworks/' \
		-s /opt/homebrew/lib -s /var/tmp/nxredux/lib \
		-s "$STAGE/tmp"
done

# Tools paks get their OWN Frameworks pool, sibling to the *.pak dirs
# (paks/Tools/Frameworks/) — NOT a second -p depth into Contents/Frameworks.
# dylibbundler bakes -p's prefix into a bundled library's *own* internal
# references too (eg. libSDL2_ttf's ref to libfreetween), not just the exe
# being fixed, and @executable_path always resolves against the process's
# *main* executable regardless of which file is doing the referencing. A
# first pass shared Contents/Frameworks between bin/ (3 levels, processed
# first) and pak dirs (5 levels, processed after with -of overwriting the
# same files): nextui.elf's own load commands stayed correct, but the
# shared libSDL2_ttf-2.0.0.dylib's *internal* freetype reference got
# overwritten to the 5-level prefix, so nextui.elf aborted at launch
# (dyld: "Library not loaded: @executable_path/../../../../../Frameworks/
# libfreetype...", 5 dots resolved from bin/'s 3-level base = outside the
# app entirely). A separate pool sidesteps the sharing conflict rather than
# fighting dylibbundler's single global -p. paks/Tools/Frameworks/ isn't
# named *.pak, so nextui's Tools listing (content.c: suffixMatch(".pak", ...))
# skips over it like any other non-pak entry.
PAK_FRAMEWORKS="$SYS/paks/Tools/Frameworks"
for exe in "$SETTINGS_ELF" "$OPTIONS_ELF" "$RATOOLS_ELF" "$SCRAPER_ELF" "$SYNC_ELF" "$EXTRAS_ELF" \
	"$GAMETIME_ELF" "$GAMETIMECTL_PAK_ELF"; do
	dylibbundler -of -cd -b -x "$exe" \
		-d "$PAK_FRAMEWORKS" -p '@executable_path/../Frameworks/' \
		-s /opt/homebrew/lib -s /var/tmp/nxredux/lib \
		-s "$STAGE/tmp"
done

# Homebrew's "sdl2" is sdl2-compat: a shim with NO SDL3 in its load commands —
# it dlopen()s "@loader_path/libSDL3.dylib" at runtime. dylibbundler only walks
# load-command dependencies (otool -L), so it never sees or copies SDL3, and the
# bundled app dies at SDL_Init with "Failed loading SDL3 library." (The dev-tree
# binary is unaffected: its shim finds SDL3 via /opt/homebrew/lib.) SDL3 links
# only system frameworks, so a plain copy into Frameworks/ — the shim's
# @loader_path — is a complete fix. Keep its valid signature; re-sign ad-hoc as
# a backstop so an arm64 sig check can never reject it.
# Two Frameworks pools (bin/'s and the Tools paks' — see above), so the
# shim needs a libSDL3.dylib next to *each* bundled libSDL2-2.0.0.dylib.
SDL3_SRC="$(brew --prefix sdl3 2>/dev/null)/lib/libSDL3.0.dylib"
[ -f "$SDL3_SRC" ] || { echo "error: SDL3 not found ($SDL3_SRC); run: brew install sdl3" >&2; exit 1; }
for fw in "$APP/Contents/Frameworks" "$PAK_FRAMEWORKS"; do
	cp "$SDL3_SRC" "$fw/libSDL3.dylib"
	codesign --force --sign - "$fw/libSDL3.dylib" 2>/dev/null || true
done

mkdir -p "$ROOT/releases"
OUT="$ROOT/releases/NXRedux-$TAG-macos-arm64.zip"
rm -f "$OUT"
ditto -c -k --keepParent "$APP" "$OUT"
echo "packaged: $OUT"
