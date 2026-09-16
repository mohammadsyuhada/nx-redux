#!/bin/sh
# Xtras catalog: gen1recomp + bundled mods (StadiumBattleFX, Running
# Shoes, Wilds of Kanto) + the default community
# mod index seeded into the game's options.lua. Mods are bundled on FRESH
# installs only; an update refreshes the game payload alone (IS_UPDATE
# below).
# Contract: idempotent; installs the LATEST upstream releases (resolved from
# the GitHub API at install time, sha256-digest-verified - fail closed);
# visible launcher registered LAST; progress on stdout; exit code is the verdict;
# every network command must carry a timeout (the caller streams our stdout
# via a blocking popen()/fgets() read loop with no watchdog of its own - an
# untimed command here stalls the whole UI, not just this script).
#
# Progress hints (Task 11): a line of the form "@NN status text" (NN = a
# bare 1-3 digit decimal 0-100, then exactly one space, then free text)
# tells the caller to set the on-screen progress bar to NN% and show
# "status text" as the current stage; any other line is ordinary
# human-readable output and is shown as the smaller detail line below the
# bar instead. Always emit a hint as its OWN line - never appended to an
# existing message line - so message strings anything else greps for
# (checksum failures, "kept", etc.) stay byte-for-byte intact. Every line, hints
# included, is still tee'd verbatim to the persistent on-device log, so
# nothing here reduces what's recorded on disk vs. what's shown on screen.
set -u

# Latest-install model (2026-08-10 spec): the game and every bundled mod
# resolve to their repos' latest GitHub release at install time -
# no pinned tags/URLs/shas. Integrity comes from the per-asset sha256 digest
# the same API responses carry (settings_updater.c's posture: an integrity
# check, not authentication). The yellow ROM manifest is fetched at the
# resolved game tag (raw.githubusercontent, no digest available - TLS-only).
GEN1_REPO="bryanthaboi/gen1recomp"
GEN1_ASSET="*rg34xxsp-stockos64-mod.zip"
# No voxel overworld mod is bundled any more (2026-09-16, user decision):
# Dramaless Shape / PotatoVoxel need far more memory than these 1GB devices
# have (a cache build OOM-killed the game even with 2GB of eMMC swap), so
# the bundle is the 2D mods only. The voxel mods stay available in the
# in-game mod browser for anyone who wants to try regardless.
# StadiumBattleFX: Stadium-style battle effects and trainer portraits
# (manifest id "STADIUM_BATTLE_FX"). Its full effect needs a Pokemon
# Stadium ROM the player
# imports in-game (a manifest required_imports entry the game prompts for)
# - without one the mod just idles, so bundling it disabled-by-default is
# safe.
STADIUM_REPO="anxiousintrovert/StadiumBattleFX"
STADIUM_ASSET="STADIUM_BATTLE_FX-*.zip"
# Running Shoes: hold-B run speed (extras all off by default; manifest id
# "running_shoes", no deps/conflicts). Contents at the zip root, ~28 KB.
SHOES_REPO="MadeinTaly/gen1recomp-running-shoes"
SHOES_ASSET="running_shoes-*.zip"
# Wilds of Kanto: visible/reactive overworld wild Pokemon (manifest id
# "overworld_wild_spawns"). Upstream renamed its release asset from
# "Wilds.of.Kanto.v*.zip" to "wilds-of-kanto-v*.zip" at v2.1.7, which made
# the old exact-case glob fail the whole install with "no matching
# download" (device-reproduced 2026-08-18) - the glob below tolerates both
# spellings. The zip contents moved in the same release: at the root now,
# previously under a "<repo>-<branch>/" source-archive wrapper - the
# install step below finds the manifest.json-bearing dir instead of
# assuming either layout. ~15 MB download, the largest of the mods.
WILDS_REPO="YoDrehDenSwagAuf/overworld-spawn-mod"
WILDS_ASSET="[Ww]ilds[.-]of[.-][Kk]anto*.zip"

# The SYSTEM-shipped 7zzs (same binary settings_updater.c extracts system
# updates with), NOT PortMaster's vendored copy: since the runtime=native
# conversion (2026-08-10) this entry has no PortMaster dependency at launch,
# so the install must not depend on it either (psp/install.sh's posture).
: "${NX_EXTRAS_UNZIP:=$SDCARD_PATH/.system/shared/bin/7zzs.aarch64}"

# TLS: verify whenever a CA bundle exists on the card; only fall back to the
# system updater's --no-check-certificate convention (common/wget_fetch.c;
# the firmware itself ships no CA bundle) when none is found. PortMaster's
# vendored bundle is used opportunistically - the runtime=native conversion
# removed the REQUIREMENT on PortMaster, not the willingness to use its
# certs when present. The .system/shared/ssl path is a seam for a future
# system-shipped bundle and wins when both exist. Verified TLS also protects
# the release-API JSON (which carries the asset digests), closing the
# digest-over-unverified-channel loop the sha256 check alone can't.
NX_WGET_TLS="--no-check-certificate"
for _ca in "$SDCARD_PATH/.system/shared/ssl/ca-certificates.crt" \
           "$SDCARD_PATH/Emus/shared/PortMaster/ssl/certs/ca-certificates.crt"; do
    if [ -f "$_ca" ]; then
        export SSL_CERT_FILE="$_ca"
        NX_WGET_TLS=""
        break
    fi
done

TMPDIR_NX="$SDCARD_PATH/.extras_tmp"
TARGET="$EXTRAS_DATA_DIR/gen1recomp"
LOVE="$TARGET/lovegame"
# Installed-version record (read by extras.elf's update check). The caller
# passes XTRAS_STATE_DIR; the fallback mirrors its device value for a bare
# environment.
: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"

# Update vs fresh install (2026-08-18): a version record from a previous
# install marks this run an UPDATE, which refreshes the game payload (and
# the Yellow manifest at the new tag) ONLY. The bundled mods are the
# player's to manage once installed - the in-game mod manager updates,
# adds and removes them (via the community index seeded below) - so an
# update never downloads or touches anything under lovegame/mods. Both
# records are checked: uninstall.sh clears both, so their joint absence is
# what distinguishes a true fresh install from an update on an older card
# that only has the legacy in-target marker.
IS_UPDATE=0
if [ -f "$XTRAS_STATE_DIR/gen1recomp.version" ] || [ -f "$TARGET/.nx_addon_version" ]; then
    IS_UPDATE=1
fi

# Set true once we start writing into $TARGET (the overlay-copy below).
# Before that point a failure leaves any prior successful install (and its
# registered launcher) fully untouched, so there is nothing to unregister.
# From that point on a failure may leave $TARGET half-overwritten, so if a
# launcher from a previous install is still sitting in $EXTRAS_ROMS_DIR we
# pull it so no broken, half-updated entry stays launchable; re-running
# install recovers everything (overlay-copy never deletes user data).
TARGET_DIRTY=0

fail() {
    echo "ERROR: $1"
    if [ "$TARGET_DIRTY" = "1" ] && [ -f "$EXTRAS_ROMS_DIR/Gen1recomp.sh" ]; then
        rm -f "$EXTRAS_ROMS_DIR/Gen1recomp.sh"
        echo "install broken, launcher removed - re-run install"
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
    # stdout indefinitely. Single-token --flag=value form so a naive "skip
    # any -* token" arg shim (see the host test) can't mis-eat a detached
    # value as the next positional arg. $NX_WGET_TLS (see its definition):
    # empty when a CA bundle was found (full TLS verification), otherwise
    # --no-check-certificate with the sha256 check below failing closed on a
    # corrupted download. Deliberately unquoted - "" must expand to no
    # argument. An empty sha (an asset the API ships no digest for, e.g. the
    # raw-hosted yellow manifest) skips the check.
    # shellcheck disable=SC2086
    wget $NX_WGET_TLS -q --timeout=30 --tries=2 -O "$2" "$1" || fail "download failed: $4 (check WiFi)"
    if [ -n "$3" ]; then
        got="$(sha256sum "$2" | cut -d' ' -f1)"
        [ "$got" = "$3" ] || fail "checksum mismatch on $4 - aborting"
    fi
}

# resolve_latest <owner/repo> <asset-glob> [fallback-glob]: queries the
# GitHub latest-release API and sets RL_TAG, RL_URL (the asset matching the
# glob) and RL_SHA (that asset's sha256 digest, "" when the API ships none).
# The JSON is scanned as a token stream - within each asset object the API
# emits "name" before "digest" before "browser_download_url", so tracking
# the last-seen name/digest pairs each URL with its own asset (same
# scraping approach settings_updater.c uses). When the primary glob matches
# nothing, the FIRST asset matching the optional fallback glob is used
# instead - the mods each ship exactly one zip per release, so "*.zip"
# there survives upstream renaming its asset (the wilds rename at v2.1.7
# turned exactly that into a whole-install "no matching download" failure).
# The game keeps no fallback: its release carries a zip per platform, and
# guessing among them would install the wrong build. Returns non-zero with
# RL_ERR set when unreachable or unparseable - callers treat that as fatal
# (`|| fail "$RL_ERR"`).
# Same helper as psp/install.sh's - catalog entries are self-contained by
# contract, so it is duplicated rather than shared.
resolve_latest() {
    _rl_json="$TMPDIR_NX/release.json"
    # shellcheck disable=SC2086  # $NX_WGET_TLS: "" must expand to no argument
    wget $NX_WGET_TLS -q --timeout=30 --tries=2 -O "$_rl_json" \
        "https://api.github.com/repos/$1/releases/latest" \
        || { RL_ERR="could not check the latest version (check WiFi)"; return 1; }
    RL_TAG="$(sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' "$_rl_json" | head -1)"
    [ -n "$RL_TAG" ] || { RL_ERR="could not read the latest version info"; return 1; }
    RL_URL=""
    RL_SHA=""
    _rl_fb_url=""
    _rl_fb_sha=""
    _rl_name=""
    _rl_digest=""
    while IFS= read -r _rl_tok; do
        case "$_rl_tok" in
            '"name":'*)   _rl_name="$(printf '%s' "$_rl_tok" | cut -d'"' -f4)"; _rl_digest="" ;;
            '"digest":'*) _rl_digest="$(printf '%s' "$_rl_tok" | cut -d'"' -f4 | sed 's/^sha256://')" ;;
            '"browser_download_url":'*)
                # shellcheck disable=SC2254  # $2/$3 are deliberately unquoted globs
                case "$_rl_name" in
                    $2) RL_URL="$(printf '%s' "$_rl_tok" | cut -d'"' -f4)"; RL_SHA="$_rl_digest"; break ;;
                esac
                if [ -z "$_rl_fb_url" ] && [ -n "${3:-}" ]; then
                    # shellcheck disable=SC2254
                    case "$_rl_name" in
                        $3) _rl_fb_url="$(printf '%s' "$_rl_tok" | cut -d'"' -f4)"; _rl_fb_sha="$_rl_digest" ;;
                    esac
                fi ;;
        esac
    done <<EOF
$(grep -o '"name": *"[^"]*"\|"digest": *"[^"]*"\|"browser_download_url": *"[^"]*"' "$_rl_json")
EOF
    if [ -z "$RL_URL" ] && [ -n "$_rl_fb_url" ]; then
        RL_URL="$_rl_fb_url"
        RL_SHA="$_rl_fb_sha"
    fi
    [ -n "$RL_URL" ] || { RL_ERR="latest release has no matching download"; return 1; }
    return 0
}

# Preflight: probe the unzip binary before touching the network. It ships
# with the system itself, so absence means damaged/stale system files, not
# a missing optional add-on. command -v covers both the absolute device
# default and a bare-name override from the host test. (The old PortMaster
# runtime preflight is gone with the runtime=native conversion - nothing
# here needs PortMaster anymore.)
if ! command -v "$NX_EXTRAS_UNZIP" >/dev/null 2>&1; then
    fail "system unzip tool missing - update or reinstall NX Redux, then retry"
fi

rm -rf "$TMPDIR_NX"
mkdir -p "$TMPDIR_NX" "$EXTRAS_DATA_DIR" || fail "cannot create install dirs"
echo "@5 Checking latest versions..."
echo "Checking latest versions..."

resolve_latest "$GEN1_REPO" "$GEN1_ASSET" || fail "$RL_ERR"
GAME_TAG="$RL_TAG"
GAME_URL="$RL_URL"
GAME_SHA="$RL_SHA"
echo "Latest game release: $GAME_TAG"

if [ "$IS_UPDATE" = "0" ]; then
    resolve_latest "$STADIUM_REPO" "$STADIUM_ASSET" "*.zip" || fail "$RL_ERR"
    STADIUM_TAG="$RL_TAG"
    STADIUM_URL="$RL_URL"
    STADIUM_SHA="$RL_SHA"
    echo "Latest stadium battle fx release: $STADIUM_TAG"

    resolve_latest "$SHOES_REPO" "$SHOES_ASSET" "*.zip" || fail "$RL_ERR"
    SHOES_TAG="$RL_TAG"
    SHOES_URL="$RL_URL"
    SHOES_SHA="$RL_SHA"
    echo "Latest running shoes release: $SHOES_TAG"

    resolve_latest "$WILDS_REPO" "$WILDS_ASSET" "*.zip" || fail "$RL_ERR"
    WILDS_TAG="$RL_TAG"
    WILDS_URL="$RL_URL"
    WILDS_SHA="$RL_SHA"
    echo "Latest wilds of kanto release: $WILDS_TAG"
fi

GEN1_YELLOW_URL="https://raw.githubusercontent.com/$GEN1_REPO/$GAME_TAG/tools/rom_manifest_yellow.json"
echo "@10 Starting install..."

fetch "$GAME_URL"        "$TMPDIR_NX/game.zip"    "$GAME_SHA" "gen1recomp $GAME_TAG (~10 MB)"
echo "@20 Downloaded gen1recomp"
fetch "$GEN1_YELLOW_URL" "$TMPDIR_NX/yellow.json" ""          "Yellow ROM manifest (~1 MB)"
echo "@25 Downloaded Yellow manifest"
if [ "$IS_UPDATE" = "0" ]; then
    fetch "$STADIUM_URL"     "$TMPDIR_NX/stadium.zip" "$STADIUM_SHA" "StadiumBattleFX $STADIUM_TAG (<1 MB)"
    echo "@35 Downloaded StadiumBattleFX"
    fetch "$SHOES_URL"       "$TMPDIR_NX/shoes.zip"   "$SHOES_SHA" "Running Shoes Mod $SHOES_TAG (<1 MB)"
    echo "@45 Downloaded Running Shoes Mod"
    fetch "$WILDS_URL"       "$TMPDIR_NX/wilds.zip"   "$WILDS_SHA" "Wilds of Kanto $WILDS_TAG (~15 MB)"
    echo "@65 Downloaded Wilds of Kanto"
fi

echo "@70 Extracting..."
echo "Extracting game..."
mkdir -p "$TMPDIR_NX/game"
extract "$TMPDIR_NX/game.zip" "$TMPDIR_NX/game" || fail "could not extract game zip"
[ -d "$TMPDIR_NX/game/gen1recomp" ] || fail "unexpected zip layout"

# Overlay-copy: the payload carries no user data (no saves, no options.lua,
# no ROM caches, no ROMs), so copying WITHOUT deleting preserves everything
# a previous install created. Never rm -rf $TARGET.
mkdir -p "$TARGET"
TARGET_DIRTY=1
cp -R "$TMPDIR_NX/game/gen1recomp/." "$TARGET/" || fail "copy into install target failed"

echo "Adding Yellow support..."
mkdir -p "$LOVE/tools"
cp "$TMPDIR_NX/yellow.json" "$LOVE/tools/rom_manifest_yellow.json" || fail "yellow manifest copy failed"

# D-pad cursor tuning (2026-08-18, user-reported): the launcher UI is a
# virtual mouse pointer. A stick scales speed with deflection, but a d-pad
# is on/off, so the stock flat 420 px/s makes every frame jump ~7px and a
# short tap travel 15-40px - small buttons are nearly untargetable on a
# stickless handheld (Brick). The patch below injects an acceleration ramp
# for the d-pad path only (taps start at 130 px/s, full speed after 0.4s
# held; a >0.25s input gap restarts the ramp) into the game's plain-Lua
# source after every payload extraction. Fail-open by design: it anchors on
# the exact upstream lines and SKIPS with a log line when they ever change,
# leaving stock behavior rather than a broken launcher. Runs on updates too
# (each install rewrites RomImporter.lua from the zip, so re-patching is
# what keeps the tune). Sticks and real mice are untouched.
patch_pad_cursor() {
    _rc="$LOVE/src/import/RomImporter.lua"
    [ -f "$_rc" ] || { echo "d-pad cursor tuning skipped (no RomImporter.lua)"; return 0; }
    grep -q 'nxDpadSpeed' "$_rc" && return 0   # already patched
    if ! grep -q '^local PAD_DPAD_SPEED = 420$' "$_rc" \
        || ! grep -q 'and PAD_SPEED or PAD_DPAD_SPEED' "$_rc"; then
        echo "d-pad cursor tuning skipped (upstream layout changed)"
        return 0
    fi
    cat > "$TMPDIR_NX/nx_dpad_helper.lua" <<'EOF'
-- NX Redux install-time patch (Xtras.pak gen1recomp install.sh): d-pad
-- cursor acceleration ramp. A d-pad has no analog deflection, so the flat
-- PAD_DPAD_SPEED above makes short taps overshoot small buttons on
-- stickless handhelds. Taps start slow and ramp to full speed while held;
-- a gap in d-pad input restarts the ramp. Stick and mouse paths untouched.
local function nxDpadSpeed(self, dt)
  local now = (love.timer and love.timer.getTime) and love.timer.getTime() or 0
  if now - (self._nxDpadLast or 0) > 0.25 then self._nxDpadRamp = 0 end
  self._nxDpadLast = now
  local ramp = math.min((self._nxDpadRamp or 0) + (dt or 0), 0.4)
  self._nxDpadRamp = ramp
  local t = ramp / 0.4
  return 130 + (420 - 130) * t * t
end
EOF
    sed -e '/^local PAD_DPAD_SPEED = 420$/r '"$TMPDIR_NX/nx_dpad_helper.lua" \
        -e 's/and PAD_SPEED or PAD_DPAD_SPEED/and PAD_SPEED or nxDpadSpeed(self, dt)/' \
        "$_rc" > "$_rc.nxtmp" && mv "$_rc.nxtmp" "$_rc" \
        || { rm -f "$_rc.nxtmp"; echo "d-pad cursor tuning skipped (patch failed)"; return 0; }
    echo "Tuned d-pad cursor for handheld precision"
}
patch_pad_cursor

# No X/Y scroll patch any more (removed 2026-09-16): the launcher gained
# native handheld controls upstream in v0.2.30 (2026-08-27) - L2/R2 scroll
# lists, L1/R1 cycle tabs, Y toggles menu navigation vs the pointer cursor,
# Start plays - which made the 2026-08-18 X/Y wheel-scroll splice obsolete
# and, worse, conflicting (Y was claimed by the toggle first, and the splice
# stole the R1 branch's return). The d-pad cursor ramp above still applies:
# it only shapes pointer-cursor mode, which upstream kept.

if [ "$IS_UPDATE" = "0" ]; then
    echo "Installing StadiumBattleFX..."
    mkdir -p "$LOVE/mods/STADIUM_BATTLE_FX"
    extract "$TMPDIR_NX/stadium.zip" "$LOVE/mods/STADIUM_BATTLE_FX" || fail "could not extract stadium battle fx mod"

    # PokePC Followers was bundled briefly on 2026-08-10 and then dropped the
    # same day (user-confirmed: Wilds of Kanto ships its own follower system
    # and the two overlap) - remove it from any install that picked it up.
    rm -rf "$LOVE/mods/pokepcfollowers"

    echo "Installing Running Shoes Mod..."
    mkdir -p "$LOVE/mods/running_shoes"
    extract "$TMPDIR_NX/shoes.zip" "$LOVE/mods/running_shoes" || fail "could not extract running shoes mod"

    echo "Installing Wilds of Kanto..."
    # Source-archive layout: find the dir holding manifest.json (the zip root
    # itself, or a single "<repo>-<branch>/" wrapper) rather than assuming one.
    mkdir -p "$TMPDIR_NX/wilds"
    extract "$TMPDIR_NX/wilds.zip" "$TMPDIR_NX/wilds" || fail "could not extract wilds of kanto mod"
    rm -f "$TMPDIR_NX/wilds.zip" # free ~15 MB before the copy below doubles the payload
    WILDS_SRC="$TMPDIR_NX/wilds"
    if [ ! -f "$WILDS_SRC/manifest.json" ]; then
        for _wd in "$TMPDIR_NX/wilds"/*/; do
            [ -f "${_wd}manifest.json" ] && WILDS_SRC="${_wd%/}" && break
        done
    fi
    [ -f "$WILDS_SRC/manifest.json" ] || fail "unexpected wilds of kanto zip layout"
    mkdir -p "$LOVE/mods/overworld_wild_spawns"
    cp -R "$WILDS_SRC/." "$LOVE/mods/overworld_wild_spawns/" || fail "could not install wilds of kanto mod"
    echo "@80 Yellow support and mods installed"
else
    # UPDATE: lovegame/mods is deliberately untouched - the player updates,
    # adds and removes mods from the in-game manager (see IS_UPDATE above).
    echo "Update: bundled mods left as-is (manage them in-game)"
    echo "@80 Yellow support updated"
fi

# Default community mod index (2026-08-18): seed bryanthaboi's index into
# options.modIndexes so the launcher's "Find mods" tab lists mods out of the
# box (upstream deliberately ships with no index and asks). options.lua is
# the game's own SaveSerializer output - deterministic "return {" first
# line, two-space indent, sorted keys, arrays as [1]-keyed tables - and its
# reader accepts only that restricted grammar, so the seed below mirrors the
# exact row SaveData would write for ModIndex.addSource("owner/repo"): feed
# (Pages URL), base, the raw.githubusercontent fallback, label, url. Rules:
# a missing file gets a minimal one (the game merges loaded keys over its
# defaults, so a modIndexes-only file is complete); a file whose
# "modIndexes = {}," line is (still) empty gets the seed spliced in; a file
# already naming this index, one holding player-added indexes (their list,
# not ours to edit), or one not starting with "return {" (a truncated write
# the game recovers from its own .bak) is left alone.
OPTS_LUA="$LOVE/options.lua"
IDX_SEED="$TMPDIR_NX/modindex_seed"
cat > "$IDX_SEED" <<'EOF'
  modIndexes = {
    [1] = {
      base = "https://bryanthaboi.github.io/gen1recomp-mod-index/",
      fallback = "https://raw.githubusercontent.com/bryanthaboi/gen1recomp-mod-index/main/site/data/index.json",
      feed = "https://bryanthaboi.github.io/gen1recomp-mod-index/data/index.json",
      label = "bryanthaboi/gen1recomp-mod-index",
      url = "bryanthaboi/gen1recomp-mod-index",
    },
  },
EOF
if [ ! -f "$OPTS_LUA" ]; then
    echo "Adding default mod index..."
    { echo "return {"; cat "$IDX_SEED"; echo "}"; } > "$OPTS_LUA" \
        || fail "could not write options.lua"
elif grep -q "gen1recomp-mod-index" "$OPTS_LUA"; then
    : # already seeded
elif grep -q '^  modIndexes = {$' "$OPTS_LUA"; then
    : # player-built index list (non-empty serializes with an open brace)
elif [ "$(head -1 "$OPTS_LUA")" = "return {" ]; then
    echo "Adding default mod index..."
    # Splice after line 1 ("return {"); a top-level key's position in the
    # file carries no meaning. Drop the empty "modIndexes = {}," line if
    # one is present (a file from before the key existed has none).
    sed -e "1r $IDX_SEED" -e '/^  modIndexes = {},$/d' "$OPTS_LUA" > "$OPTS_LUA.nxtmp" \
        && mv "$OPTS_LUA.nxtmp" "$OPTS_LUA" || fail "could not update options.lua"
fi

# Legacy swapfile cleanup (2026-09-16): earlier launchers created an eMMC
# swapfile for the voxel overworld mods (/swapfile, later /mnt/UDISK/
# swapfile[.new]). Those mods are no longer bundled and the base game needs
# no swap (116MB peak RSS in play on the Brick), so reclaim the space -
# the last version cost 2GB of UDISK plus eMMC wear. Non-fatal throughout.
# NX_SWAP_ROOT is a test seam (the host test can't touch the real /).
for _sf in "${NX_SWAP_ROOT:-}/swapfile" "${NX_SWAP_ROOT:-}/mnt/UDISK/swapfile" "${NX_SWAP_ROOT:-}/mnt/UDISK/swapfile.new"; do
    [ -e "$_sf" ] || continue
    if grep -q "^$_sf " /proc/swaps 2>/dev/null; then
        swapoff "$_sf" 2>/dev/null || { echo "  swapfile $_sf still in use - left in place"; continue; }
    fi
    rm -f "$_sf" && echo "Removed legacy swapfile $_sf"
done

echo "@90 Scanning for ROMs..."
echo "Looking for your Pokemon ROMs..."
# Recognized by SHA-256 via the same busybox sha256sum fetch() already depends
# on - every card that can run this install can run the scan. (The game's own
# importer gates on SHA-1 internally; these are the same US dumps it accepts
# - Red, Blue, Yellow, and since upstream's Gen 2 support Gold, Silver and
# Crystal 1.1 (2026-09-16, hashed from dumps whose SHA-1 matched upstream's
# GameVersion.lua) - just hashed with the tool the device actually ships.
# sha1sum only exists inside PortMaster's vendored bin, which runtime=native
# users don't have - a card without it used to skip this scan entirely.)
# Known gap: the Crystal 1.0 dump (SHA-1 f4cd194b...) has no verified SHA-256
# on record, so it is not auto-copied - the game still imports it by hand.
found=0
for dir in "$SDCARD_PATH/Roms/Game Boy (GB)" "$SDCARD_PATH/Roms/Game Boy Color (GBC)"; do
    [ -d "$dir" ] || continue
    for rom in "$dir"/*.gb "$dir"/*.gbc; do
        [ -f "$rom" ] || continue
        [ -f "$LOVE/$(basename "$rom")" ] && { found=$((found+1)); continue; }
        case "$(sha256sum "$rom" | cut -d' ' -f1)" in
            5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b|\
            2a951313c2640e8c2cb21f25d1db019ae6245d9c7121f754fa61afd7bee6452d|\
            8cbaa499397e4f1a679c992ea9382a2dd7942ab398b48c19829c2d9529de47bf|\
            fb0016d27b1e5374e1ec9fcad60e6628d8646103b5313ca683417f52b97e7e4e|\
            72b190859a59623cbef6c49d601f8de52c1d2331b4f08a8d2acc17274fc19a8c|\
            fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2)
                echo "  found $(basename "$rom")"
                cp "$rom" "$LOVE/" && found=$((found+1))
                ;;
        esac
    done
done
[ "$found" -eq 0 ] && echo "  none found - copy a US Red/Blue/Yellow/Gold/Silver/Crystal ROM into Roms/Xtra Games (EXTRAS)/.data/gen1recomp/lovegame/ later"

# Self-install the EXTRAS platform runtime. The skeleton SYSTEM tree
# normally ships this pak out of the box (skeleton/SYSTEM/<plat>/paks/
# Emus/EXTRAS.pak/launch.sh, same script as extras_games_launch.sh below)
# - this block is the repair path for a card whose skeleton is missing or
# stale (e.g. an older release, or a card migrated from the legacy
# platform-tag layout) rather than the normal install-time trigger.
# Installed under .system/paks/Emus - nextui's first-priority PAKS_PATH
# lookup location (checked before the flat Emus/ and platform-subfolder
# Emus/$PLATFORM/ fallbacks, see hasEmu() in content.c) - not
# Emus/$PLATFORM, which is the community-pak convention, not where system-
# provided runtimes like this one belong.
PAK_DIR="$(cd "$(dirname "$CATALOG_DIR")/.." && pwd)"   # .../Xtras.pak
EMU_PAK="$SDCARD_PATH/.system/paks/Emus/EXTRAS.pak"
if [ ! -f "$EMU_PAK/launch.sh" ] && [ -f "$PAK_DIR/extras_games_launch.sh" ]; then
    echo "Registering Xtra Games platform..."
    mkdir -p "$EMU_PAK"
    cp "$PAK_DIR/extras_games_launch.sh" "$EMU_PAK/launch.sh" || fail "could not install EXTRAS.pak"
fi

# Both records carry the resolved game tag: the XTRAS_STATE_DIR file is what
# extras.elf's update check reads; the legacy in-target marker keeps an
# older Xtras.pak (pre-update-tracking) reading this install as installed.
mkdir -p "$XTRAS_STATE_DIR" || fail "cannot create version state dir"
printf '%s\n' "$GAME_TAG" > "$XTRAS_STATE_DIR/gen1recomp.version" || fail "could not write version record"
printf '%s\n' "$GAME_TAG" > "$TARGET/.nx_addon_version" || fail "could not write version marker"

# Display alias: the launcher filename would render as "Gen1recomp" in the
# game list; a map.txt line (filename<TAB>alias - content.c Directory_index)
# sets the shown name without renaming the file, so resume slots, this
# script's own cleanup paths, and existing installs stay keyed to
# Gen1recomp.sh. Upsert keyed on the filename so other entries' alias lines
# in the shared folder are preserved. Written before the launcher lands so
# the entry never appears under the wrong name.
MAP="$EXTRAS_ROMS_DIR/map.txt"
TAB="$(printf '\t')"
{
    [ -f "$MAP" ] && grep -v "^Gen1recomp\.sh$TAB" "$MAP"
    printf 'Gen1recomp.sh\tPokemon Gen1Recomp++\n'
} > "$MAP.tmp" && mv "$MAP.tmp" "$MAP" || fail "could not write display name"

# LAST STEP: the visible menu entry. Everything above must already be good.
cp "$CATALOG_DIR/files/Gen1recomp.sh" "$EXTRAS_ROMS_DIR/Gen1recomp.sh" || fail "could not register launcher"

rm -rf "$TMPDIR_NX"
echo "@100 Done"
echo "Done. Find Gen1recomp under Xtra Games."
exit 0
