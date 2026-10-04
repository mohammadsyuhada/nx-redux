#!/bin/sh
# Xtras catalog: Jeod's Retro Handheld Ports (RHH), a third-party PortMaster
# catalog. Drops the catalog's source file into PortMaster's config/, where
# harbourmaster loads every *.source.json; PortMaster then lists the ports
# and refreshes the catalog itself (hourly, when opened), so new ports need
# no update here. A version bump here re-copies the source file (resetting
# harbourmaster's cache in it) and re-fetches gmtoolkit.
#
# gmtoolkit: the RHH GameMaker ports' first-run patchers call
# $controlfolder/gmtoolkit.aarch64, which PortMaster does not ship (RHH's own
# installer, Pharos, fetches it). Installed from the LATEST JeodC/gmtoolkit
# release, resolved from the GitHub API at install time and sha256-digest-
# verified - fail closed. Unlike portmaster/install.sh (which runs before any
# CA bundle is on the card), TLS is verified too, against the bundle the
# PortMaster install put in place: the digest comes from the same API
# response, so it only means something over an authenticated connection.
# The system wget ships with certificate checks off, hence the explicit
# --check-certificate=on. Every network command carries a timeout (the caller
# streams our stdout through a blocking read loop with no watchdog of its own).
#
# It does NOT write the version marker: this is an internal entry, so
# extras.elf writes $XTRAS_STATE_DIR/portmaster-rhh.version after a
# successful install. Runs under extras.elf's scrubbed env (SDCARD_PATH/
# PLATFORM/LOGS_PATH/CATALOG_DIR/XTRAS_STATE_DIR).
set -u

GMT_REPO="JeodC/gmtoolkit"
GMT_ASSET="gmtoolkit-aarch64.zip"
: "${NX_EXTRAS_UNZIP:=$SDCARD_PATH/.system/shared/bin/7zzs.aarch64}"

TMPDIR_NX="$SDCARD_PATH/.extras_tmp"
PM_DIR="$SDCARD_PATH/Emus/shared/PortMaster"
SRC="030_rhh.source.json"
CA="$PM_DIR/ssl/certs/ca-certificates.crt"

fail() {
    echo "ERROR: $1"
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
    wget --check-certificate=on --ca-certificate="$CA" -q --timeout=30 --tries=2 -O "$2" "$1" || fail "download failed: $4 (check WiFi)"
    if [ -n "$3" ]; then
        got="$(sha256sum "$2" | cut -d' ' -f1)"
        [ "$got" = "$3" ] || fail "checksum mismatch on $4 - aborting"
    fi
}

# resolve_latest <owner/repo> <asset-glob>: sets RL_TAG, RL_URL and RL_SHA
# from the GitHub latest-release API (see portmaster/install.sh for how the
# token stream pairs each URL with its own asset's digest).
resolve_latest() {
    _rl_json="$TMPDIR_NX/release.json"
    wget --check-certificate=on --ca-certificate="$CA" -q --timeout=30 --tries=2 -O "$_rl_json" \
        "https://api.github.com/repos/$1/releases/latest" \
        || fail "could not check the latest gmtoolkit (check WiFi)"
    RL_TAG="$(sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' "$_rl_json" | head -1)"
    [ -n "$RL_TAG" ] || fail "could not read the latest gmtoolkit info"
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
    [ -n "$RL_URL" ] || fail "the latest gmtoolkit release has no matching download"
}

# Preflights, all before touching the network.
[ -f "$CATALOG_DIR/$SRC" ] || fail "catalog payload missing ($SRC)"
[ -f "$PM_DIR/pugwash" ] || fail "install PortMaster first (Xtras, Tools tab)"
[ -s "$CA" ] || fail "PortMaster's certificate bundle is missing - reinstall PortMaster, then retry"
command -v "$NX_EXTRAS_UNZIP" >/dev/null 2>&1 \
    || fail "system unzip tool missing - update or reinstall NX Redux, then retry"

rm -rf "$TMPDIR_NX"
mkdir -p "$TMPDIR_NX" || fail "cannot create install dirs"

echo "@10 Checking the latest gmtoolkit..."
resolve_latest "$GMT_REPO" "$GMT_ASSET"
echo "gmtoolkit release: $RL_TAG"

echo "@20 Downloading gmtoolkit..."
fetch "$RL_URL" "$TMPDIR_NX/$GMT_ASSET" "$RL_SHA" "gmtoolkit (~10 MB)"

echo "@70 Installing gmtoolkit..."
extract "$TMPDIR_NX/$GMT_ASSET" "$TMPDIR_NX/gmt" || fail "could not extract gmtoolkit"
[ -f "$TMPDIR_NX/gmt/gmtoolkit.aarch64" ] || fail "unexpected gmtoolkit download (no gmtoolkit.aarch64)"
# Copied to a temp name and renamed, so a failed copy never leaves a
# truncated binary where the ports look for it.
cp -f "$TMPDIR_NX/gmt/gmtoolkit.aarch64" "$PM_DIR/gmtoolkit.aarch64.nxtmp" || fail "could not install gmtoolkit"
mv -f "$PM_DIR/gmtoolkit.aarch64.nxtmp" "$PM_DIR/gmtoolkit.aarch64" || fail "could not install gmtoolkit"
chmod +x "$PM_DIR/gmtoolkit.aarch64" 2>/dev/null || true
[ -f "$TMPDIR_NX/gmt/gmtoolkit.LICENSE.txt" ] && cp -f "$TMPDIR_NX/gmt/gmtoolkit.LICENSE.txt" "$PM_DIR/"
rm -rf "$TMPDIR_NX"

echo "@90 Adding the RHH catalog to PortMaster..."
mkdir -p "$PM_DIR/config" || fail "cannot create PortMaster's config folder"
cp -f "$CATALOG_DIR/$SRC" "$PM_DIR/config/$SRC" || fail "could not add the catalog"

echo "@100 Done"
echo "Added. Open PortMaster to browse the RHH ports."
exit 0
