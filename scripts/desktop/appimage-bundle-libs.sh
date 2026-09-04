#!/bin/sh
# Copy the shared-library closure of the AppDir's ELF binaries into
# $APPDIR/usr/lib -- minus everything that must come from the HOST.
# Container-only, called from package-appimage.sh step 3.
# Usage: appimage-bundle-libs.sh <APPDIR>
#
# Why the exclusions matter: AppRun puts usr/lib first on LD_LIBRARY_PATH, so
# a bundled copy shadows the host's. Right for SDL2 & co, fatal for anything
# the host's graphics driver co-loads into our process. v1.9.0 swept in
# Ubuntu 22.04's libstdc++.so.6 (GLIBCXX <= 3.4.30) plus libGL/libGLX/
# libGLdispatch; on any host with Mesa >= 25 (Ubuntu 24.04.4's HWE stack,
# Mint 22, 25.10) the driver's libLLVM.so.20 demands GLIBCXX_3.4.32, refuses
# to load, no GLX visual can be found, SDL_CreateWindow (and so the renderer)
# comes back NULL and nextui died before opening a window (issue #86).
#
# Policy = the AppImage community excludelist (appimage-excludelist, vendored
# verbatim) + the driver-adjacent families below, minus the two font libs in
# KEEP. Every excluded library is something any X11/Wayland desktop already
# has (they are hard deps of Mesa, libSDL2, GTK, or glibc itself).
set -eu
APPDIR="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"

# Vendored sonames -> one anchored ERE alternation (strip comments, escape
# regex metachars: libstdc++.so.6 -> libstdc\+\+\.so\.6).
LIST="$(sed 's/#.*//; s/[[:space:]]*$//' "$HERE/appimage-excludelist" | grep -v '^$' | sed 's/[.+]/\\&/g' | paste -sd'|' -)"

# Families the host GL stack co-loads that the list only names piecemeal:
# glvnd vendor libs + GLES/EGL, Mesa internals (libgallium, libglapi), the
# driver's LLVM and its closure (libffi/libzstd/libelf/libsensors/libxml2/
# libedit/libtinfo), DRM/GBM/Vulkan, the X11/xcb libs Mesa's GLX links
# (libX11, libX11-xcb, libxcb-*, libXext, libXfixes, libXxf86vm, libxshmfence,
# and xcb's own libXau/libXdmcp) and the whole wayland client/server/egl set.
# libXcursor/libXi/libXinerama/libXrandr/libXrender/libXss stay bundled: only
# our SDL2 uses them, nothing driver-side does, and they are ABI-stable.
FAMILIES='libGLX_.*|libEGL.*|libGLES.*|libOpenGL.*|libgallium.*|libglapi.*|libLLVM.*|libffi\.so.*|libzstd\.so.*|libelf\.so.*|libsensors\.so.*|libxml2\.so.*|libedit\.so.*|libtinfo\.so.*|libdrm.*|libgbm.*|libvulkan.*|libX11.*|libXau.*|libXdmcp.*|libXext.*|libXfixes.*|libXxf86vm.*|libxcb.*|libxshmfence.*|libwayland-.*'

# On the upstream list (for GTK/Qt/pango interplay we do not have) but bundled
# here on purpose: only SDL2_ttf -- itself bundled and built against them --
# uses them in-process, nothing host-side co-loads them, and a minimal host
# (or a container) may not have them at all.
KEEP='libfreetype\.so\.6|libharfbuzz\.so\.0'

# usr/lib/*.so* first: package-appimage.sh pre-seeds it with our in-tree
# libs (libmsettings.so, libchdr.so.0, libgametimedb.so), which ldd reports
# as "not found" from the elfs -- and so never lists what they need
# (libgametimedb.so -> libsqlite3.so.0). Sweeping them directly closes that
# gap. The glob is expanded once, before the loop, so libs this sweep copies
# in are not themselves re-swept (ldd already follows their closure).
mkdir -p "$APPDIR/usr/lib"
for bin in "$APPDIR"/usr/lib/*.so* "$APPDIR"/usr/system/bin/*.elf "$APPDIR"/usr/system/cores/*.so "$APPDIR"/usr/system/paks/Tools/*/*.elf "$APPDIR"/usr/system/shared/bin/rsync "$APPDIR"/usr/system/shared/bin/7zz; do
	[ -e "$bin" ] || continue
	ldd "$bin" 2>/dev/null | awk '/=> \//{print $3}' | while read -r lib; do
		name="$(basename "$lib")"
		if printf '%s\n' "$name" | grep -Eq "^($KEEP)$"; then
			:
		elif printf '%s\n' "$name" | grep -Eq "^($LIST|$FAMILIES)$"; then
			continue
		fi
		cp -n "$lib" "$APPDIR/usr/lib/" || true
	done
done
