#!/bin/sh
# Assemble NXRedux.app from built desktop artifacts. Run via: make package-macos
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TAG="$(cd "$ROOT" && git describe --tags --abbrev=0 2>/dev/null || echo untagged)"
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

# binaries + cores
cp "$ROOT/workspace/all/nextui/build/desktop/nextui.elf" "$SYS/bin/"
cp "$ROOT/workspace/all/minarch/build/desktop/minarch.elf" "$SYS/bin/"
install -m 0755 "$ROOT/scripts/desktop/check-update.sh" "$SYS/bin/"
for c in gambatte mgba; do
	cp "$ROOT/workspace/desktop/cores/output/${c}_libretro.so" "$SYS/cores/"
done

# libmsettings.so is linked in with a bare (path-less) install name — dylibbundler
# can locate it via -s but chokes rewriting its rpath, so give the reference an
# absolute path before bundling. Work on a disposable copy, not the build
# output under workspace/, so its rpath-strip/re-sign below (and dylibbundler's
# own re-sign) never touch a file other dev workflows might load directly.
mkdir -p "$STAGE/tmp"
LIBMSETTINGS="$STAGE/tmp/libmsettings.so"
cp "$ROOT/workspace/desktop/libmsettings/libmsettings.so" "$LIBMSETTINGS"
for exe in "$SYS/bin/nextui.elf" "$SYS/bin/minarch.elf"; do
	install_name_tool -change libmsettings.so "$LIBMSETTINGS" "$exe"
done

# gcc auto-embeds several LC_RPATH entries (its own toolchain lib dirs, plus a
# bare @loader_path/$ORIGIN default) in every binary it links — nextui.elf,
# minarch.elf, and libmsettings.so alike. dylibbundler individually rewrites
# each to the same bundled path below, producing duplicate LC_RPATH commands
# dyld refuses to load ("duplicate LC_RPATH ... in <binary>"). Strip them
# first; dylibbundler adds back exactly one bundled rpath for whichever
# binary actually needs it (minarch, for @rpath/libchdr.0.so).
for bin in "$SYS/bin/nextui.elf" "$SYS/bin/minarch.elf" "$LIBMSETTINGS"; do
	otool -l "$bin" | awk '
		/LC_RPATH/ { f=1; next }
		f && /path/ { sub(/^ *path /, ""); sub(/ \(offset.*/, ""); print; f=0 }
	' | while IFS= read -r rp; do
		install_name_tool -delete_rpath "$rp" "$bin"
	done
done

# base skeleton + entry + metadata
cp -R "$ROOT/skeleton/BASE" "$APP/Contents/Resources/base-skeleton"
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
for exe in "$SYS/bin/nextui.elf" "$SYS/bin/minarch.elf"; do
	dylibbundler -of -cd -b -x "$exe" \
		-d "$APP/Contents/Frameworks" -p '@executable_path/../../../Frameworks/' \
		-s /opt/homebrew/lib -s /var/tmp/nxredux/lib \
		-s "$STAGE/tmp"
done

mkdir -p "$ROOT/releases"
OUT="$ROOT/releases/NXRedux-$TAG-macos-arm64.zip"
rm -f "$OUT"
ditto -c -k --keepParent "$APP" "$OUT"
echo "packaged: $OUT"
