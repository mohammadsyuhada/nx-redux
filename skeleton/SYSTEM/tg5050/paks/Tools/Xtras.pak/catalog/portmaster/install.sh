#!/bin/sh
# Xtras catalog: PortMaster (ports GUI + runtime).
# Contract: idempotent; installs the LATEST upstream release (resolved from
# the GitHub API at install time, sha256-digest-verified - fail closed);
# progress on stdout ("@NN status text" hints - see gen1recomp/install.sh's
# header for the full contract text); exit code is the verdict; every
# network command must carry a timeout (the caller streams our stdout via a
# blocking popen()/fgets() read loop with no watchdog of its own).
#
# This script replaces the install flow the old Tools/PortMaster.pak elf
# carried (the pak used to ship in SYSTEM and download PortMaster itself on
# first open). What lands where:
#   - PortMaster.zip (pugwash GUI)  -> Emus/shared/PortMaster/ (upstream's
#     own PortMaster/ zip root, extracted into Emus/shared/)
#   - runtime deps (bin/lib/libs/pylibs zips, cert, patch helper) already
#     ship on every card in Emus/shared/PortMaster/files/ (skeleton BASE) -
#     extracted/copied into the runtime tree here, exactly as the old elf's
#     extract_deps() did (compat-lib quarantine, unversioned .so copies,
#     busybox wrappers included)
#   - Emus/PORTS.pak/launch.sh (the Ports console runner) <- pak/ports_launch.sh
#   - Tools/PortMaster.pak (the GUI tool pak: launch.sh + ports_launch.sh +
#     portmaster.elf) <- $CATALOG_DIR/pak/ - the FLAT location: getTools()
#     (nextui/content.c) lists flat SD paks first, and migrate-paks.sh only
#     deletes a flat name while .system ships a same-named pak, which this
#     build no longer does. The platform subfolder is the community-pak
#     convention for paks that hardcode it internally; this one is
#     location-independent, and one card serves one platform anyway.
# The NxRedux patches (control.txt, device_info.txt, platform.py,
# hardware.py, mod_TrimUI.txt) are NOT applied here: portmaster.elf
# re-applies all of them before/after every pugwash run, so first launch
# patches a fresh install by itself.
#
# Reinstall/update over an existing install is safe: config/config.json,
# the layout marker, installed ports and their saves all live outside the
# paths this overwrites (-aoa extraction only refreshes runtime files).
set -u

# Latest-install model (2026-08-10 spec): the release to install is resolved
# from the GitHub latest-release API at install time - no pinned tag/URL/sha
# constants. Integrity comes from the per-asset sha256 digest the same API
# response carries (settings_updater.c's posture: an integrity check, not
# authentication - trust rests on the upstream project's GitHub account).
PM_REPO="PortsMaster/PortMaster-GUI"
PM_ASSET="PortMaster.zip"

# The SYSTEM-shipped 7zzs (skeleton/SYSTEM/shared/bin - the same binary
# settings_updater.c extracts system updates with). PortMaster's own
# vendored copy can't be used: it is part of what this script installs.
: "${NX_EXTRAS_UNZIP:=$SDCARD_PATH/.system/shared/bin/7zzs.aarch64}"

TMPDIR_NX="$SDCARD_PATH/.extras_tmp"
SHARED_EMUS="$SDCARD_PATH/Emus/shared"
PM_DIR="$SHARED_EMUS/PortMaster"
PM_FILES="$PM_DIR/files"
PORTS_PAK="$SDCARD_PATH/Emus/PORTS.pak"
TOOLS_PAK="$SDCARD_PATH/Tools/PortMaster.pak"
# Earlier revisions of this entry installed to the platform subfolder;
# absorbed into the flat location on install (see the migration block below).
OLD_TOOLS_PAK="$SDCARD_PATH/Tools/$PLATFORM/PortMaster.pak"
# Installed-version record (read by extras.elf's update check). The caller
# passes XTRAS_STATE_DIR; the fallback mirrors its device value for a bare
# environment.
: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"
USERDATA_DIR="$SDCARD_PATH/.userdata/$PLATFORM"

# Set true once extraction starts writing into $PM_DIR. Before that point a
# failure leaves any prior successful install fully untouched. From then on
# a failure may leave the runtime half-written - a truncated pugwash must
# not stay launchable - so fail() strips the runtime back to the BASE-
# shipped files/ + patchedScripts/ and pulls both launchers (same shape as
# uninstall.sh; ports/saves in Roms are never touched). Re-running install
# recovers cleanly.
TARGET_DIRTY=0

cleanup_runtime() {
    for entry in "$PM_DIR"/* "$PM_DIR"/.[!.]*; do
        [ -e "$entry" ] || continue
        case "$(basename "$entry")" in
            files|patchedScripts) ;;
            *) rm -rf "$entry" ;;
        esac
    done
    rm -rf "$PORTS_PAK" "$TOOLS_PAK" "$OLD_TOOLS_PAK"
}

fail() {
    echo "ERROR: $1"
    if [ "$TARGET_DIRTY" = "1" ]; then
        cleanup_runtime
        echo "install broken, runtime removed - re-run install"
    fi
    rm -rf "$TMPDIR_NX"
    exit 1
}

extract() { # zip dest
    case "$NX_EXTRAS_UNZIP" in
        *7zzs*) "$NX_EXTRAS_UNZIP" x -y -o"$2" "$1" >/dev/null || return 1 ;;
        *)      "$NX_EXTRAS_UNZIP" -q -o "$1" -d "$2" || return 1 ;;
    esac
}

fetch() { # url dest sha256(""=skip) label
    echo "Downloading $4..."
    # --timeout=30 bounds dns/connect/read all at once (GNU wget); --tries=2
    # caps retries, so a stalled/half-open connection gives up in ~60s
    # instead of hanging the popen'd read loop that streams this script's
    # stdout indefinitely. --no-check-certificate is the system updater's own
    # convention (common/wget_fetch.c): the firmware ships no CA bundle and
    # the sha256 digest check below fails closed on a corrupted download. An
    # empty sha (an asset the API ships no digest for) skips the check -
    # TLS-only, same as the updater when a release carries no hash.
    wget --no-check-certificate -q --timeout=30 --tries=2 -O "$2" "$1" || fail "download failed: $4 (check WiFi)"
    if [ -n "$3" ]; then
        got="$(sha256sum "$2" | cut -d' ' -f1)"
        [ "$got" = "$3" ] || fail "checksum mismatch on $4 - aborting"
    fi
}

# resolve_latest <owner/repo> <asset-glob>: queries the GitHub latest-release
# API and sets RL_TAG, RL_URL (the asset matching the glob) and RL_SHA (that
# asset's sha256 digest, "" when the API ships none). The JSON is scanned as
# a token stream - within each asset object the API emits "name" before
# "digest" before "browser_download_url", so tracking the last-seen name/
# digest pairs each URL with its own asset (same scraping approach
# settings_updater.c uses). Fails closed when unreachable or unparseable.
resolve_latest() {
    _rl_json="$TMPDIR_NX/release.json"
    wget --no-check-certificate -q --timeout=30 --tries=2 -O "$_rl_json" \
        "https://api.github.com/repos/$1/releases/latest" \
        || fail "could not check the latest version (check WiFi)"
    RL_TAG="$(sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' "$_rl_json" | head -1)"
    [ -n "$RL_TAG" ] || fail "could not read the latest version info"
    RL_URL=""
    RL_SHA=""
    _rl_name=""
    _rl_digest=""
    while IFS= read -r _rl_tok; do
        case "$_rl_tok" in
            '"name":'*)   _rl_name="$(printf '%s' "$_rl_tok" | cut -d'"' -f4)"; _rl_digest="" ;;
            '"digest":'*) _rl_digest="$(printf '%s' "$_rl_tok" | cut -d'"' -f4 | sed 's/^sha256://')" ;;
            '"browser_download_url":'*)
                # shellcheck disable=SC2254  # $2 is deliberately an unquoted glob
                case "$_rl_name" in
                    $2) RL_URL="$(printf '%s' "$_rl_tok" | cut -d'"' -f4)"; RL_SHA="$_rl_digest"; break ;;
                esac ;;
        esac
    done <<EOF
$(grep -o '"name": *"[^"]*"\|"digest": *"[^"]*"\|"browser_download_url": *"[^"]*"' "$_rl_json")
EOF
    [ -n "$RL_URL" ] || fail "latest release has no matching download"
}

# Preflights, all before touching the network. The unzip binary and the
# runtime-dep bundle both ship with the system/BASE itself, so absence means
# damaged/stale system files, not a missing optional add-on. The pak payload
# ships inside this catalog entry.
if ! command -v "$NX_EXTRAS_UNZIP" >/dev/null 2>&1; then
    fail "system unzip tool missing - update or reinstall NX Redux, then retry"
fi
for f in bin.zip lib.zip libs.zip pylibs.zip ca-certificates.crt disable_python_function.py; do
    [ -f "$PM_FILES/$f" ] || fail "bundled runtime files missing ($f) - reinstall the NX Redux BASE files, then retry"
done
for f in launch.sh ports_launch.sh portmaster.elf; do
    [ -f "$CATALOG_DIR/pak/$f" ] || fail "catalog pak payload missing ($f)"
done

rm -rf "$TMPDIR_NX"
mkdir -p "$TMPDIR_NX" || fail "cannot create install dirs"
echo "@5 Checking latest version..."
echo "Checking latest version..."

resolve_latest "$PM_REPO" "$PM_ASSET"
echo "Latest release: $RL_TAG"
echo "@10 Starting install..."

fetch "$RL_URL" "$TMPDIR_NX/PortMaster.zip" "$RL_SHA" "PortMaster $RL_TAG (~24 MB)"
echo "@45 Downloaded PortMaster"

echo "@50 Extracting PortMaster..."
echo "Extracting PortMaster..."
mkdir -p "$SHARED_EMUS"
TARGET_DIRTY=1
# The zip root is PortMaster/ (verified against upstream releases; the old
# elf extracted it into Emus/shared/ the same way).
extract "$TMPDIR_NX/PortMaster.zip" "$SHARED_EMUS" || fail "could not extract PortMaster.zip"
rm -f "$TMPDIR_NX/PortMaster.zip"
[ -f "$PM_DIR/pugwash" ] || fail "unexpected PortMaster.zip layout (no pugwash)"

echo "@55 Extracting binaries..."
echo "Extracting binaries..."
mkdir -p "$PM_DIR/bin"
extract "$PM_FILES/bin.zip" "$PM_DIR/bin" || fail "could not extract bundled binaries"

echo "@65 Extracting libraries..."
echo "Extracting libraries..."
mkdir -p "$PM_DIR/lib"
extract "$PM_FILES/lib.zip" "$PM_DIR/lib" || fail "could not extract bundled libraries"

echo "@75 Extracting additional libraries..."
echo "Extracting additional libraries..."
mkdir -p "$PM_DIR/libs"
extract "$PM_FILES/libs.zip" "$PM_DIR/libs" || fail "could not extract additional libraries"

echo "@85 Extracting Python libraries..."
echo "Extracting Python libraries..."
# pylibs.zip carries exlibs/ and pylibs/ at its root
extract "$PM_FILES/pylibs.zip" "$PM_DIR" || fail "could not extract Python libraries"

mkdir -p "$PM_DIR/ssl/certs"
cp -f "$PM_FILES/ca-certificates.crt" "$PM_DIR/ssl/certs/ca-certificates.crt" || fail "could not install SSL certificates"
cp -f "$PM_FILES/disable_python_function.py" "$PM_DIR/disable_python_function.py" || fail "could not install patch helper"

echo "@90 Configuring PortMaster..."
echo "Configuring PortMaster..."

# Quarantine compat libs (newer glib, fontconfig, freetype, brotli) into
# lib/compat/. These override system libs and break pugwash's SDL2 stack, so
# they must only be in the LD_LIBRARY_PATH for port launches (set in
# ports_launch.sh), not pugwash.
mkdir -p "$PM_DIR/lib/compat"
(cd "$PM_DIR/lib" && \
    for f in libglib-2.0.so* libfontconfig.so* libfreetype.so* libbrotlidec.so* libbrotlicommon.so*; do
        [ -f "$f" ] && mv -f "$f" compat/
    done) 2>/dev/null || true

# Create unversioned .so copies for libs that only have versioned names
# (FAT32/exFAT don't support symlinks, and some ports link against
# unversioned .so names).
# In lib/: skip GL/EGL/DRM libs - unversioned copies override system GPU
# drivers and crash gl4es.
# In lib/compat/: create all unversioned copies (ports need them and compat
# isn't in pugwash's path).
(cd "$PM_DIR/lib" && for f in *.so.*; do
    case "$f" in libEGL*|libGL*|libdrm*|libOpenGL*) continue ;; esac # libGL* covers GLU/GLX/GLd too
    base="$(echo "$f" | sed 's/\.so\..*/.so/')"
    [ ! -e "$base" ] && cp -f "$f" "$base"
done) 2>/dev/null || true
(cd "$PM_DIR/lib/compat" && for f in *.so.*; do
    base="$(echo "$f" | sed 's/\.so\..*/.so/')"
    [ ! -e "$base" ] && cp -f "$f" "$base"
done) 2>/dev/null || true

# 7zzs doesn't reliably carry zip exec bits (same blanket chmod the old
# elf's PATCHING step ran over the whole runtime tree)
chmod -R +x "$PM_DIR" 2>/dev/null || true

# Busybox applet wrappers: ports expect standalone coreutils on PATH, the
# bundle ships one busybox. A wrapper is (re)written when the name is free
# or already holds an earlier generation of itself; real binaries are never
# clobbered. sh is skipped - the wrapper would exec through busybox sh and
# ports need the system shell.
BB="$PM_DIR/bin/busybox"
if [ -f "$BB" ]; then
    (cd "$PM_DIR/bin" && created='' && \
    for cmd in $("$BB" --list); do
        case "$cmd" in sh) continue ;; esac
        if [ ! -e "$cmd" ] || grep -q 'exec .*/busybox .*\$@' "$cmd" 2>/dev/null; then
            printf '#!/bin/sh\nexec %s %s "$@"\n' "$BB" "$cmd" > "$cmd"
            created="$created $cmd"
        fi
    done
    # shellcheck disable=SC2086  # word-splitting the collected names is the point
    [ -n "$created" ] && chmod +x $created
    touch busybox_wrappers.done) 2>/dev/null || true
fi

# Default pugwash config: skip the disclaimer/theme first-run prompts. Only
# on a fresh install - reinstall/update keeps the user's live config.
if [ ! -f "$PM_DIR/config/config.json" ]; then
    mkdir -p "$PM_DIR/config"
    cat > "$PM_DIR/config/config.json" <<'EOF'
{
    "disclaimer": true,
    "show_experimental": false,
    "theme": "default_theme",
    "theme-scheme": "Darkest Mode"
}
EOF
fi

echo "@95 Installing launchers..."
echo "Installing launchers..."

# The Ports console runner: Emus/PORTS.pak/launch.sh is a copy of
# ports_launch.sh (what the old elf's create_ports_pak() did).
mkdir -p "$PORTS_PAK" || fail "cannot create PORTS.pak"
cp -f "$CATALOG_DIR/pak/ports_launch.sh" "$PORTS_PAK/launch.sh" || fail "could not create the Ports console launcher"
chmod +x "$PORTS_PAK/launch.sh" 2>/dev/null || true

# The GUI tool pak, at the flat Tools location (see header).
mkdir -p "$TOOLS_PAK" || fail "cannot create the PortMaster tool pak"
cp -f "$CATALOG_DIR/pak/launch.sh" "$CATALOG_DIR/pak/ports_launch.sh" "$CATALOG_DIR/pak/portmaster.elf" "$TOOLS_PAK/" \
    || fail "could not install the PortMaster tool pak"
chmod +x "$TOOLS_PAK/launch.sh" "$TOOLS_PAK/ports_launch.sh" "$TOOLS_PAK/portmaster.elf" 2>/dev/null || true

# Migration: drop any platform-subfolder copy an earlier revision installed,
# then rmdir (not rm -rf) the platform dir so unrelated user paks survive.
rm -rf "$OLD_TOOLS_PAK"
rmdir "$SDCARD_PATH/Tools/$PLATFORM" 2>/dev/null || true

# Where installed ports land - skeleton/BASE ships this folder, so this is
# just repair for a card it was deleted from. The console only appears in
# the menu once at least one port is installed.
mkdir -p "$SDCARD_PATH/Roms/Ports (PORTS)"

# The Ports console and the new Tools pak must show up without a reboot.
rm -f "$USERDATA_DIR/emulist_cache.txt" "$USERDATA_DIR/romindex_cache.txt"

mkdir -p "$XTRAS_STATE_DIR" || fail "cannot create version state dir"
printf '%s\n' "$RL_TAG" > "$XTRAS_STATE_DIR/portmaster.version" || fail "could not write version record"

rm -rf "$TMPDIR_NX"
echo "@100 Done"
echo "Done. Open PortMaster from Tools to browse ports."
exit 0
