#!/bin/sh
# Xtras catalog: standalone PPSSPP emulator (ben16w/minui-psp's PSP.pak).
# Contract: idempotent; installs the LATEST upstream release (resolved from
# the GitHub API at install time, sha256-digest-verified - fail closed);
# progress on stdout ("@NN status text" hints - see gen1recomp/install.sh's
# header for the full contract text); exit code is the verdict; every
# network command must carry a timeout (the caller streams our stdout via a
# blocking popen()/fgets() read loop with no watchdog of its own).
#
# Install location: $SDCARD_PATH/Emus/$PLATFORM/PSP.pak - upstream's own
# community-pak convention, which nextui's getEmuPath()/hasEmu()
# (common/utils.c, nextui/content.c) resolve as a fallback after the flat
# Emus/PSP.pak location. The payload is installed VERBATIM, launch.sh
# included: upstream's launch.sh hardcodes that platform path, and shipping
# our own copy (the pre-2026-09-16 flat-location design) meant re-syncing
# it by hand on every upstream release - which silently didn't happen when
# minui-psp 6.x added the Brick "setalpha 0" display fix, leaving Brick
# users with a black screen. Nothing in this catalog entry overrides any
# upstream file any more. A legacy flat Emus/PSP.pak install from that
# earlier design is migrated: on a fresh install its PPSSPP config tree
# (settings, textures, cheat toggles) is carried over, then the flat copy is
# removed - it MUST go, since nextui would otherwise pick it first.
set -u

# Latest-install model (2026-08-10 spec): the release to install is resolved
# from the GitHub latest-release API at install time - no pinned tag/URL/sha
# constants. Integrity comes from the per-asset sha256 digest the same API
# response carries (settings_updater.c's posture: an integrity check, not
# authentication - trust rests on the upstream author's GitHub account).
PSP_REPO="ben16w/minui-psp"
PSP_ASSET="PSP.pak.zip"

# The SYSTEM-shipped 7zzs (skeleton/SYSTEM/shared/bin - the same binary
# settings_updater.c extracts system updates with), NOT PortMaster's vendored
# copy: unlike gen1recomp, psp needs no PortMaster runtime at launch, so the
# install must not depend on it either.
: "${NX_EXTRAS_UNZIP:=$SDCARD_PATH/.system/shared/bin/7zzs.aarch64}"

TMPDIR_NX="$SDCARD_PATH/.extras_tmp"
TARGET="$SDCARD_PATH/Emus/$PLATFORM/PSP.pak"
OLD_PAK="$SDCARD_PATH/Emus/PSP.pak" # legacy flat install (before 2026-09-16)
# Installed-version record (read by extras.elf's update check). The caller
# passes XTRAS_STATE_DIR; the fallback mirrors its device value for a bare
# environment.
: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"

# Set true once we start writing into $TARGET (the overlay-copy below).
# Before that point a failure leaves any prior successful install fully
# untouched. From that point on a failure may leave $TARGET half-written -
# a truncated emulator binary must not stay launchable - so fail() pulls
# the pak's launch.sh (what getEmuPath()/hasEmu() probe for), which hides
# the PSP console until a re-run install repairs everything. The overlay-
# copy never deletes user data, so re-running install recovers cleanly.
TARGET_DIRTY=0

fail() {
    echo "ERROR: $1"
    if [ "$TARGET_DIRTY" = "1" ] && [ -f "$TARGET/launch.sh" ]; then
        rm -f "$TARGET/launch.sh"
        echo "install broken, pak launcher removed - re-run install"
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
    # convention (common/wget_fetch.c): the firmware ships no CA bundle
    # (verified on-device: TLS fails without one) and the sha256 digest check
    # below fails closed on a corrupted download. An empty sha (an asset the
    # API ships no digest for) skips the check - TLS-only, same as the
    # updater when a release carries no hash.
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

# Preflight: probe the unzip binary before touching the network. It ships
# with the system itself, so absence means damaged/stale system files, not a
# missing optional add-on. command -v covers both the absolute device
# default and a bare-name override from the host test.
if ! command -v "$NX_EXTRAS_UNZIP" >/dev/null 2>&1; then
    fail "system unzip tool missing - update or reinstall NX Redux, then retry"
fi

rm -rf "$TMPDIR_NX"
mkdir -p "$TMPDIR_NX" || fail "cannot create install dirs"
echo "@5 Checking latest version..."
echo "Checking latest version..."

resolve_latest "$PSP_REPO" "$PSP_ASSET"
echo "Latest release: $RL_TAG"
echo "@10 Starting install..."

fetch "$RL_URL" "$TMPDIR_NX/pak.zip" "$RL_SHA" "PPSSPP $RL_TAG (~31 MB)"
echo "@55 Downloaded PPSSPP"

echo "@70 Extracting..."
echo "Extracting emulator..."
mkdir -p "$TMPDIR_NX/pak"
extract "$TMPDIR_NX/pak.zip" "$TMPDIR_NX/pak" || fail "could not extract pak zip"
rm -f "$TMPDIR_NX/pak.zip" # free ~31 MB before the copy below doubles the payload
# The zip is flat (launch.sh/PPSSPP/bin/ at the archive root, no PSP.pak/
# wrapper directory) - verified against the 5.1.0 and 6.2.0 release assets.
[ -f "$TMPDIR_NX/pak/launch.sh" ] && [ -d "$TMPDIR_NX/pak/PPSSPP" ] || fail "unexpected zip layout"

echo "@80 Installing..."
echo "Installing to Emus/$PLATFORM/PSP.pak..."
# Reinstall/update: keep the user's live PPSSPP config tree (in-emulator
# settings, texture packs, cheat toggles) rather than clobbering it with
# the payload's pristine copy - drop .config from the payload instead.
# (Payload-side .config updates, e.g. a newer cheat.db, don't apply on
# reinstall; acceptable for keeping every user setting.) FRESH_CONFIG also
# gates the migration below: only a target WITHOUT its own config adopts
# the legacy flat install's one.
FRESH_CONFIG=1
if [ -d "$TARGET/PPSSPP/.config" ]; then
    FRESH_CONFIG=0
    rm -rf "$TMPDIR_NX/pak/PPSSPP/.config"
fi
mkdir -p "$TARGET"
TARGET_DIRTY=1
cp -R "$TMPDIR_NX/pak/." "$TARGET/" || fail "copy into install target failed"
# busybox unzip/7zzs don't reliably carry zip exec bits
chmod +x "$TARGET/launch.sh" "$TARGET/PPSSPP/PPSSPPSDL_"* "$TARGET/bin/minui-power-control" "$TARGET/bin/setalpha" 2>/dev/null || true

# Migration: remove any legacy flat Emus/PSP.pak so exactly one PSP.pak
# remains and nextui resolves the new one (saves already live outside the
# pak - launch.sh bind-mounts Saves/PSP and the shared savestate dir - so
# only the config tree needs carrying over).
if [ -d "$OLD_PAK" ]; then
    echo "Migrating old Emus/PSP.pak..."
    if [ "$FRESH_CONFIG" = "1" ] && [ -d "$OLD_PAK/PPSSPP/.config" ]; then
        echo "  keeping its PPSSPP settings/textures/cheats"
        rm -rf "$TARGET/PPSSPP/.config"
        cp -R "$OLD_PAK/PPSSPP/.config" "$TARGET/PPSSPP/.config" || fail "could not carry over old PPSSPP config"
    fi
    rm -rf "$OLD_PAK"
    echo "  old install removed"
fi

# Where games go - skeleton/BASE ships this folder (exact name matters: a
# differently-spelled sibling would show as a duplicate (PSP) console), so
# this is just repair for a card it was deleted from. The console only
# appears in the menu once at least one game is in it.
mkdir -p "$SDCARD_PATH/Roms/Sony Playstation Portable (PSP)"

mkdir -p "$XTRAS_STATE_DIR" || fail "cannot create version state dir"
printf '%s\n' "$RL_TAG" > "$XTRAS_STATE_DIR/psp.version" || fail "could not write version record"

rm -rf "$TMPDIR_NX"
echo "@100 Done"
echo "Done. Put PSP games in Roms/Sony Playstation Portable (PSP)."
exit 0
