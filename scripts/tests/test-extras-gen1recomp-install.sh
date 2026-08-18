#!/usr/bin/env bash
# Host-side test for the gen1recomp Xtras catalog installer.
# Network and device binaries are shimmed via PATH; all paths are sandboxed.
# A && pass "..." || fail "..." is used throughout below: pass()/fail() only
# printf and never fail, so the || branch never runs when the check is true.
# shellcheck disable=SC2015
# Single quotes are deliberate in the grep patterns below: they must match
# literal $SHDIR/$GAMEDIR text inside the installed launcher file, not
# expand against this script's own variables.
# shellcheck disable=SC2016
set -u

FAILS=0
say()  { printf '%s\n' "$*"; }
pass() { say "PASS: $*"; }
fail() { say "FAIL: $*"; FAILS=$((FAILS+1)); }

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENTRY="$ROOT/skeleton/SYSTEM/tg5050/paks/Tools/Xtras.pak/catalog/gen1recomp"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---- fixtures ---------------------------------------------------------
# Fake upstream game zip: Gen1recomp.sh + gen1recomp/{bin,libs.aarch64,lovegame}
FIX="$TMP/fix"
mkdir -p "$FIX/gen1recomp/bin" "$FIX/gen1recomp/libs.aarch64" \
         "$FIX/gen1recomp/lovegame/tools" "$FIX/gen1recomp/lovegame/src"
echo 'upstream-launcher' > "$FIX/Gen1recomp.sh"
echo 'love-bin-v1'       > "$FIX/gen1recomp/bin/love.aarch64"
echo 'liblove'           > "$FIX/gen1recomp/libs.aarch64/liblove-11.5.so"
echo 'main'              > "$FIX/gen1recomp/lovegame/main.lua"
echo '{"red":1}'         > "$FIX/gen1recomp/lovegame/tools/rom_manifest.json"
# Minimal RomImporter.lua carrying the exact anchor lines the two
# install-time patches key on: the d-pad cursor tuning (nxDpadSpeed after
# the constant, speed-pick line rewritten) and the X/Y wheel-scroll splices
# (after _cycleTab(1), gamepadreleased's opening line, and the righty read).
mkdir -p "$FIX/gen1recomp/lovegame/src/import"
cat > "$FIX/gen1recomp/lovegame/src/import/RomImporter.lua" <<'EOF'
local PAD_DEAD = 0.28
local PAD_SPEED = 560   -- px/s at full stick deflection
local PAD_DPAD_SPEED = 420
function RomImporter:_updatePadCursor(dt)
    local speed = (math.abs(ax) > PAD_DEAD or math.abs(ay) > PAD_DEAD)
      and PAD_SPEED or PAD_DPAD_SPEED
  local ry = self._padAxis.righty or 0
end
function RomImporter:gamepadpressed(_, button)
  elseif button == "rightshoulder" then
    self:_cycleTab(1)
  end
end
function RomImporter:gamepadreleased(_, button)
end
EOF
(cd "$FIX" && zip -qr "$TMP/game.zip" Gen1recomp.sh gen1recomp)

echo '{"yellow":1}' > "$TMP/rom_manifest_yellow.json"

# Dramaless Shape fixture (replaced the dead-upstream DramaticShapeVoxelMod
# 2026-08-18): contents at the zip root.
DRAMALESSFIX="$TMP/dramalessfix"
mkdir -p "$DRAMALESSFIX/assets"
echo '{"id":"DRAMALESS_SHAPE"}' > "$DRAMALESSFIX/manifest.json"
echo 'voxels' > "$DRAMALESSFIX/assets/a.bin"
(cd "$DRAMALESSFIX" && zip -qr "$TMP/dramaless.zip" manifest.json assets)

# StadiumBattleFX fixture: contents at the zip root.
STADIUMFIX="$TMP/stadiumfix"
mkdir -p "$STADIUMFIX"
echo '{"id":"STADIUM_BATTLE_FX"}' > "$STADIUMFIX/manifest.json"
echo 'lua' > "$STADIUMFIX/main.lua"
(cd "$STADIUMFIX" && zip -qr "$TMP/stadium.zip" manifest.json main.lua)

# Running Shoes fixture: versioned asset name, contents at the zip root.
SHOESFIX="$TMP/shoesfix"
mkdir -p "$SHOESFIX"
echo '{"id":"running_shoes"}' > "$SHOESFIX/manifest.json"
echo 'lua' > "$SHOESFIX/main.lua"
(cd "$SHOESFIX" && zip -qr "$TMP/shoes.zip" manifest.json main.lua)

# Wilds of Kanto fixture: a GitHub SOURCE archive - everything under a
# "<repo>-<branch>/" wrapper dir, NOT at the zip root - so this exercises
# install.sh's manifest.json-locating step, not just a plain extract.
# (Upstream moved to a root-layout zip at v2.1.7 - the plain-extract path
# the other mod fixtures already cover - but the locator must keep handling
# the wrapper form for any older asset.)
WILDSFIX="$TMP/wildsfix"
mkdir -p "$WILDSFIX/overworld-spawn-mod-main/assets"
echo '{"id":"overworld_wild_spawns"}' > "$WILDSFIX/overworld-spawn-mod-main/manifest.json"
echo 'lua'    > "$WILDSFIX/overworld-spawn-mod-main/main.lua"
echo 'sprite' > "$WILDSFIX/overworld-spawn-mod-main/assets/a.png"
(cd "$WILDSFIX" && zip -qr "$TMP/wilds.zip" overworld-spawn-mod-main)

sha() { shasum -a 256 "$1" | cut -d' ' -f1; }

# Fake GitHub latest-release API responses. Decoy assets sit FIRST so the
# "matched by glob, paired with its own digest" logic is actually exercised.
# Field order inside each asset (name -> digest -> browser_download_url)
# mirrors the real API - resolve_latest's token-stream walk depends on it.
write_game_json() { # digest
  cat > "$TMP/game_release.json" <<EOF
{
  "tag_name": "v9.9.9",
  "name": "Release v9.9.9",
  "assets": [
    {
      "name": "gen1recomp-9.9.9-linux.zip",
      "digest": "sha256:1111111111111111111111111111111111111111111111111111111111111111",
      "browser_download_url": "https://github.com/bryanthaboi/gen1recomp/releases/download/v9.9.9/gen1recomp-9.9.9-linux.zip"
    },
    {
      "name": "gen1recomp-9.9.9-rg34xxsp-stockos64-mod.zip",
      "digest": "sha256:$1",
      "browser_download_url": "https://github.com/bryanthaboi/gen1recomp/releases/download/v9.9.9/gen1recomp-9.9.9-rg34xxsp-stockos64-mod.zip"
    }
  ]
}
EOF
}
# DRAMALESS_ASSET_NAME override: test 3b serves the same payload under a
# name the primary glob does NOT match, proving resolve_latest's
# sole-zip-fallback survives an upstream asset rename (the wilds v2.1.7
# rename is exactly the failure class this guards against).
write_dramaless_json() { # digest
  _dn="${DRAMALESS_ASSET_NAME:-DRAMALESS_SHAPE-2.0.2.zip}"
  cat > "$TMP/dramaless_release.json" <<EOF
{
  "tag_name": "v2.0.2",
  "name": "v2.0.2 - Hotfix",
  "assets": [
    {
      "name": "$_dn",
      "digest": "sha256:$1",
      "browser_download_url": "https://github.com/artyrambles/DRAMALESS_SHAPE/releases/download/v2.0.2/$_dn"
    }
  ]
}
EOF
}
write_stadium_json() { # digest
  cat > "$TMP/stadium_release.json" <<EOF
{
  "tag_name": "v2.1.7",
  "name": "StadiumBattleFX 2.1.7",
  "assets": [
    {
      "name": "STADIUM_BATTLE_FX-2.1.7.zip",
      "digest": "sha256:$1",
      "browser_download_url": "https://github.com/anxiousintrovert/StadiumBattleFX/releases/download/v2.1.7/STADIUM_BATTLE_FX-2.1.7.zip"
    }
  ]
}
EOF
}
write_shoes_json() { # digest
  cat > "$TMP/shoes_release.json" <<EOF
{
  "tag_name": "v1.4.1",
  "name": "running_shoes 1.4.1",
  "assets": [
    {
      "name": "running_shoes-1.4.1.zip",
      "digest": "sha256:$1",
      "browser_download_url": "https://github.com/MadeinTaly/gen1recomp-running-shoes/releases/download/v1.4.1/running_shoes-1.4.1.zip"
    }
  ]
}
EOF
}
# The asset carries the RENAMED (v2.1.7+) lowercase-hyphen name: upstream's
# switch away from "Wilds.of.Kanto.v*.zip" is exactly what broke the old
# exact-case glob with "no matching download" (device-reproduced
# 2026-08-18), so the fixture must prove the tolerant glob matches the new
# spelling.
write_wilds_json() { # digest
  cat > "$TMP/wilds_release.json" <<EOF
{
  "tag_name": "v2.1.7",
  "name": "Wilds of Kanto 2.1.7",
  "assets": [
    {
      "name": "wilds-of-kanto-v2.1.7.zip",
      "digest": "sha256:$1",
      "browser_download_url": "https://github.com/YoDrehDenSwagAuf/overworld-spawn-mod/releases/download/v2.1.7/wilds-of-kanto-v2.1.7.zip"
    }
  ]
}
EOF
}

# ---- sandbox SD card --------------------------------------------------
SD="$TMP/sd"
GB_DIR="$SD/Roms/Game Boy (GB)"
GBC_DIR="$SD/Roms/Game Boy Color (GBC)"
mkdir -p "$GB_DIR" "$GBC_DIR" "$SD/.userdata/tg5050/logs"
# A "Red" ROM whose sha256 we control via the sha256sum shim below.
echo 'red-rom' > "$GB_DIR/Pokemon Red.gb"
echo 'junk'    > "$GB_DIR/Tetris.gb"

# ---- PATH shims -------------------------------------------------------
BIN="$TMP/bin"
mkdir -p "$BIN"
# wget shim: serves fixtures by matching the URL tail.
cat > "$BIN/wget" <<SHIM
#!/usr/bin/env bash
out=""; url=""
while [ \$# -gt 0 ]; do
  case "\$1" in
    -O) out="\$2"; shift ;;
    -*) ;;
    *) url="\$1" ;;
  esac
  shift
done
case "\$url" in
  *bryanthaboi/gen1recomp/releases/latest) cp "$TMP/game_release.json" "\$out" ;;
  *DRAMALESS_SHAPE/releases/latest)  cp "$TMP/dramaless_release.json" "\$out" ;;
  *StadiumBattleFX/releases/latest)  cp "$TMP/stadium_release.json" "\$out" ;;
  *gen1recomp-running-shoes/releases/latest) cp "$TMP/shoes_release.json" "\$out" ;;
  *overworld-spawn-mod/releases/latest)    cp "$TMP/wilds_release.json" "\$out" ;;
  *stockos64-mod.zip)        cp "$TMP/game.zip" "\$out" ;;
  *rom_manifest_yellow.json) cp "$TMP/rom_manifest_yellow.json" "\$out" ;;
  *DRAMALESS_SHAPE-*.zip)    cp "$TMP/dramaless.zip" "\$out" ;;
  *mystery-voxel-*.zip)      cp "$TMP/dramaless.zip" "\$out" ;;  # renamed-asset fallback (test 3b)
  *STADIUM_BATTLE_FX-*.zip)  cp "$TMP/stadium.zip" "\$out" ;;
  *running_shoes-*.zip)      cp "$TMP/shoes.zip" "\$out" ;;
  *wilds-of-kanto-*.zip)     cp "$TMP/wilds.zip" "\$out" ;;
  *) echo "wget shim: unknown url \$url" >&2; exit 1 ;;
esac
SHIM
chmod +x "$BIN/wget"
# sha256sum shim -> host shasum (macOS has no sha256sum), EXCEPT the Red ROM
# fixture, which reports the pinned real-dump sha256 the install.sh ROM scan
# whitelists (the scan is sha256-keyed since 2026-08-10 - sha1sum only ever
# existed inside PortMaster's vendored bin, which runtime=native users don't
# have). Everything else (download digest checks) hashes for real.
cat > "$BIN/sha256sum" <<SHIM
#!/usr/bin/env bash
for f in "\$@"; do
  case "\$f" in
    *"Pokemon Red.gb")
      echo "5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b  \$f"
      exit 0 ;;
  esac
done
shasum -a 256 "\$@"
SHIM
chmod +x "$BIN/sha256sum"

run_install() {
  # Regenerate the API fixtures each call: digests default to the CURRENT
  # payload hashes (test 2 rebuilds game.zip; test 4 corrupts dramaless.zip
  # and relies on its hash being recomputed so the failure lands at extract,
  # not fetch), and the GAME_DIGEST override lets the mismatch scenario
  # serve a bad digest without touching the happy-path plumbing.
  write_game_json "${GAME_DIGEST:-$(sha "$TMP/game.zip")}"
  write_dramaless_json "${DRAMALESS_DIGEST:-$(sha "$TMP/dramaless.zip")}"
  write_stadium_json "${STADIUM_DIGEST:-$(sha "$TMP/stadium.zip")}"
  write_shoes_json "${SHOES_DIGEST:-$(sha "$TMP/shoes.zip")}"
  write_wilds_json "${WILDS_DIGEST:-$(sha "$TMP/wilds.zip")}"
  PATH="$BIN:$PATH" \
  PLATFORM="${PLATFORM_OVERRIDE:-tg5050}" \
  SDCARD_PATH="$SD" \
  LOGS_PATH="$SD/.userdata/tg5050/logs" \
  EXTRAS_ROMS_DIR="$SD/Roms/Xtra Games (EXTRAS)" \
  EXTRAS_DATA_DIR="$SD/Roms/Xtra Games (EXTRAS)/.data" \
  CATALOG_DIR="$ENTRY" \
  XTRAS_STATE_DIR="$SD/.userdata/shared/xtras" \
  NX_EXTRAS_UNZIP="${NX_EXTRAS_UNZIP_OVERRIDE:-unzip}" \
  bash "$ENTRY/install.sh"
}

G="$SD/Roms/Xtra Games (EXTRAS)/.data/gen1recomp"

# ---- 0. preflight: system unzip tool probe ----------------------------
# Since the runtime=native conversion (2026-08-10) nothing here depends on
# PortMaster; the only preflight is the SYSTEM-shipped 7zzs (psp/install.sh
# posture). A missing/broken system tree must abort before any network use.
if NX_EXTRAS_UNZIP_OVERRIDE="$TMP/no-such-7zzs" run_install > "$TMP/log0.txt" 2>&1; then
  fail "preflight: missing system unzip did not abort"
else pass "preflight: missing system unzip aborts"; fi
grep -q 'system unzip tool missing' "$TMP/log0.txt" \
  && pass "preflight: message printed" || fail "preflight: message missing"
! grep -q 'Downloading' "$TMP/log0.txt" \
  && pass "preflight: no download attempted" || fail "preflight: a download was attempted despite missing unzip"
[ ! -d "$G" ] \
  && pass "preflight: nothing written to install target" || fail "preflight: install target created despite missing unzip"
[ ! -e "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" ] \
  && pass "preflight: no launcher registered" || fail "preflight: launcher registered despite missing unzip"

# ---- 1. fresh install -------------------------------------------------
if run_install > "$TMP/log1.txt" 2>&1; then pass "fresh install exits 0"
else fail "fresh install exited non-zero: $(tail -3 "$TMP/log1.txt")"; fi
[ -f "$G/bin/love.aarch64" ]                        && pass "runtime extracted"        || fail "runtime missing"
[ -f "$G/lovegame/tools/rom_manifest_yellow.json" ] && pass "yellow manifest in place" || fail "yellow manifest missing"
# D-pad cursor tuning: the helper injected after the constant, the
# speed-pick line rewritten to call it, and the install announcing it.
grep -q 'local function nxDpadSpeed' "$G/lovegame/src/import/RomImporter.lua" \
                                                     && pass "d-pad cursor helper injected" || fail "d-pad cursor helper missing"
grep -q 'and PAD_SPEED or nxDpadSpeed(self, dt)' "$G/lovegame/src/import/RomImporter.lua" \
                                                     && pass "d-pad speed-pick line rewritten" || fail "d-pad speed-pick line not rewritten"
! grep -q 'and PAD_SPEED or PAD_DPAD_SPEED' "$G/lovegame/src/import/RomImporter.lua" \
                                                     && pass "stock flat d-pad speed no longer referenced" || fail "stock d-pad speed line still present"
grep -q 'Tuned d-pad cursor' "$TMP/log1.txt"         && pass "d-pad tuning announced" || fail "d-pad tuning not announced"
# X/Y wheel-scroll patch: all three splices landed (press mapping, release
# clear, held-scroll in the update loop) and the install announced it.
[ "$(grep -c '_nxWheelHold' "$G/lovegame/src/import/RomImporter.lua")" = "5" ] \
                                                     && pass "X/Y scroll splices all landed" || fail "X/Y scroll splices missing or partial"
grep -q 'elseif button == "x" or button == "y" then' "$G/lovegame/src/import/RomImporter.lua" \
                                                     && pass "X/Y press mapping injected" || fail "X/Y press mapping missing"
grep -q 'Mapped X/Y to list scrolling' "$TMP/log1.txt" \
                                                     && pass "X/Y scroll patch announced" || fail "X/Y scroll patch not announced"
[ -f "$G/lovegame/mods/DRAMALESS_SHAPE/manifest.json" ] && pass "dramaless shape mod installed" || fail "dramaless shape mod missing"
[ -f "$G/lovegame/mods/STADIUM_BATTLE_FX/manifest.json" ] && pass "stadium battle fx mod installed" || fail "stadium battle fx mod missing"
[ -f "$G/lovegame/mods/running_shoes/manifest.json" ] && pass "running shoes mod installed" || fail "running shoes mod missing"
# Wrapper dir must be stripped: manifest.json at the mod root, not nested.
[ -f "$G/lovegame/mods/overworld_wild_spawns/manifest.json" ] \
                                                     && pass "wilds of kanto mod installed (wrapper stripped)" || fail "wilds of kanto mod missing or still wrapped"
[ ! -d "$G/lovegame/mods/overworld_wild_spawns/overworld-spawn-mod-main" ] \
                                                     && pass "wilds of kanto wrapper dir not nested" || fail "wilds of kanto wrapper dir leaked into mod folder"
[ -f "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" ] && pass "launcher registered"     || fail "launcher missing"
# The EXTRAS platform runtime self-installs under .system/paks/Emus -
# nextui's first-priority PAKS_PATH lookup location (hasEmu() in
# content.c checks it before the flat Emus/ and platform-subfolder
# Emus/$PLATFORM/ fallbacks) - not under Emus/$PLATFORM, which is the
# community-pak convention.
[ -f "$SD/.system/paks/Emus/EXTRAS.pak/launch.sh" ] \
  && pass "EXTRAS.pak self-installed under .system/paks/Emus" \
  || fail "EXTRAS.pak not self-installed under .system/paks/Emus"
[ ! -e "$SD/Emus/tg5050/EXTRAS.pak" ] \
  && pass "EXTRAS.pak not leaked into legacy Emus/\$PLATFORM location" \
  || fail "EXTRAS.pak wrongly installed at legacy Emus/\$PLATFORM location"
grep -q 'GAMEDIR="$SHDIR/.data/gen1recomp"' \
  "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh"      && pass "launcher is nx-patched"  || fail "launcher not patched"
# TLS trust: the firmware ships no CA store, so the launcher must hand the
# game's curl a CA bundle (system .system/shared/ssl first, PortMaster
# fallback) or every in-game https fetch (mod index etc) dies on cert
# verification.
grep -q 'CURL_CA_BUNDLE' "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" \
                                                     && pass "launcher exports a CA bundle for in-game fetches" || fail "launcher missing CURL_CA_BUNDLE export"
grep -q '.system/shared/ssl/ca-certificates.crt' "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" \
                                                     && pass "launcher prefers the system-shipped CA bundle" || fail "launcher does not reference the system CA bundle"
# runtime=native contract: extras_games_launch.sh execs the entry directly
# only when this marker sits in the script's first 20 lines.
head -20 "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" | grep -q '^# NX_RUNTIME: native' \
                                                     && pass "launcher carries the native runtime marker" || fail "native runtime marker missing from launcher"
# First-launch stall fix (2026-08-10): the 512MB swapfile dd must run in a
# backgrounded subshell (closing "') &'" at column 0), not in the game's
# critical path - in the foreground it was 10-20s of black screen that read
# as a broken launch.
grep -q '^) &' "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" \
                                                     && pass "launcher swapfile block is backgrounded" || fail "launcher swapfile block runs in the foreground"
# Display alias (map.txt is filename<TAB>alias, content.c Directory_index):
# without it the list shows the launcher basename "Gen1recomp" (lowercase r).
MAPFILE="$SD/Roms/Xtra Games (EXTRAS)/map.txt"
TAB="$(printf '\t')"
[ "$(grep -c "^Gen1recomp\.sh$TAB" "$MAPFILE" 2>/dev/null)" = "1" ] \
                                                     && pass "display alias written once"  || fail "display alias missing or duplicated"
grep -q "^Gen1recomp\.sh${TAB}Gen1Recomp (Pokemon R/B/Y)\$" "$MAPFILE" \
                                                     && pass "display alias has the right name" || fail "display alias name wrong"
[ -f "$G/lovegame/Pokemon Red.gb" ]                  && pass "ROM scan copied Red"      || fail "Red not copied"
[ ! -f "$G/lovegame/Tetris.gb" ]                     && pass "ROM scan skipped junk"    || fail "junk ROM copied"
[ "$(cat "$G/.nx_addon_version" 2>/dev/null)" = "v9.9.9" ] \
                                                     && pass "version marker written"  || fail "version marker wrong"
[ "$(cat "$SD/.userdata/shared/xtras/gen1recomp.version" 2>/dev/null)" = "v9.9.9" ] \
                                                     && pass "version record written"  || fail "version record wrong"
grep -q 'Latest game release: v9.9.9' "$TMP/log1.txt" \
                                                     && pass "resolved game release announced" || fail "no resolved game release line"
grep -q 'Latest dramaless shape release: v2.0.2' "$TMP/log1.txt" \
                                                     && pass "resolved dramaless shape release announced" || fail "no resolved dramaless shape release line"
grep -q 'Latest stadium battle fx release: v2.1.7' "$TMP/log1.txt" \
                                                     && pass "resolved stadium battle fx release announced" || fail "no resolved stadium battle fx release line"
grep -q 'Latest running shoes release: v1.4.1' "$TMP/log1.txt" \
                                                     && pass "resolved running shoes release announced" || fail "no resolved running shoes release line"
# The renamed lowercase-hyphen wilds asset (the fixture serves it) must
# resolve - this is the exact regression that surfaced on-device as
# "ERROR: latest release has no matching download".
grep -q 'Latest wilds of kanto release: v2.1.7' "$TMP/log1.txt" \
                                                     && pass "resolved wilds release announced (renamed asset matched)" || fail "no resolved wilds release line - renamed asset glob broken"
# Task 11: install.sh's "@NN status text" progress hints - a bare "@90 " (NN
# followed by a space) proves the hint syntax parses correctly, and that the
# original human-readable message on the very next line ("Looking for your
# Pokemon ROMs...", asserted just below) still stands unprefixed/untouched.
grep -q '@90 ' "$TMP/log1.txt"                       && pass "install emits @NN progress hints" \
                                                      || fail "no @NN progress hint found in install output"
grep -q '^Looking for your Pokemon ROMs\.\.\.$' "$TMP/log1.txt" \
                                                     && pass "hint line doesn't clobber the adjacent message line" \
                                                     || fail "message line after a hint was altered"

# ---- 1b. default mod index seeded into options.lua ----------------------
# A fresh install has no options.lua, so the installer must write a minimal
# one holding only the seeded modIndexes row - in the game's own
# SaveSerializer shape ("return {" first line, [1]-keyed array row) so its
# restricted-grammar reader parses it and mergeOptions fills in the rest.
OPTS_LUA="$G/lovegame/options.lua"
[ -f "$OPTS_LUA" ] \
  && pass "fresh install seeded options.lua" || fail "fresh install left no options.lua"
[ "$(head -1 "$OPTS_LUA" 2>/dev/null)" = "return {" ] \
  && pass "seeded options.lua starts with the serializer's return line" || fail "seeded options.lua has the wrong first line"
grep -q 'feed = "https://bryanthaboi.github.io/gen1recomp-mod-index/data/index.json"' "$OPTS_LUA" \
  && pass "seeded options.lua names the default index feed" || fail "default index feed missing from seeded options.lua"
grep -q 'fallback = "https://raw.githubusercontent.com/bryanthaboi/gen1recomp-mod-index/main/site/data/index.json"' "$OPTS_LUA" \
  && pass "seeded row carries the raw fallback URL" || fail "seeded row is missing the raw fallback URL"
grep -q '\[1\] = {' "$OPTS_LUA" \
  && pass "seeded row uses the serializer's bracket-keyed array form" || fail "seeded row not bracket-keyed"
# Voxel mod ships disabled on ALL platforms - the shared enablement answer
# is seeded false (per-game toggles in-game override it).
grep -q '^    DRAMALESS_SHAPE = false,$' "$OPTS_LUA" \
  && pass "voxel mod seeded disabled" || fail "voxel mod not seeded disabled"
# Handheld performance defaults are tg5040-only (the Brick's weaker GPU) -
# this fresh install ran as tg5050, so none of them may appear.
! grep -q '^    DRAMALESS_SHAPE = {$' "$OPTS_LUA" \
  && pass "perf defaults: not seeded on tg5050" || fail "perf defaults: leaked onto tg5050"
! grep -q 'fpsCap' "$OPTS_LUA" \
  && pass "perf defaults: fps cap untouched on tg5050" || fail "perf defaults: fps cap leaked onto tg5050"

# ---- 1c. tg5040 fresh install seeds the performance defaults ------------
# Same fresh install on the Brick platform: the voxel mod's GPU-heavy
# defaults tuned down and the FPS cap set to what its GPU can hold.
rm -rf "$SD/Roms/Xtra Games (EXTRAS)" "$SD/.userdata/shared/xtras"
if PLATFORM_OVERRIDE=tg5040 run_install > "$TMP/log1c.txt" 2>&1; then pass "tg5040 fresh install exits 0"
else fail "tg5040 fresh install exited non-zero: $(tail -3 "$TMP/log1c.txt")"; fi
grep -q '^    DRAMALESS_SHAPE = {$' "$OPTS_LUA" \
  && pass "tg5040: mod options bucket seeded" || fail "tg5040: mod options bucket missing"
grep -q 'renderDistanceSetting = 16,' "$OPTS_LUA" \
  && pass "tg5040: render distance SHORT" || fail "tg5040: render distance not seeded"
grep -q 'shadowQuality = "off",' "$OPTS_LUA" \
  && pass "tg5040: shadows off" || fail "tg5040: shadows not seeded"
grep -q 'water = "sky",' "$OPTS_LUA" \
  && pass "tg5040: water reflections SKY" || fail "tg5040: water not seeded"
grep -q '^  fpsCap = 40,$' "$OPTS_LUA" \
  && pass "tg5040: fps cap 40 seeded" || fail "tg5040: fps cap missing"
grep -q 'feed = "https://bryanthaboi.github.io/gen1recomp-mod-index/data/index.json"' "$OPTS_LUA" \
  && pass "tg5040: mod index seeded alongside perf defaults" || fail "tg5040: mod index missing"

# ---- 2. update (version record present) refreshes the game ONLY --------
# The baseline install left version records, so this run takes the UPDATE
# path: game payload + Yellow manifest refreshed, user data preserved, and
# lovegame/mods left ENTIRELY alone - bundled mods are the player's to
# manage in-game once installed (2026-08-18 spec). Every mods/ state a play
# session could have produced must survive: a player-removed bundled mod
# stays removed, an in-place player edit stays, and even the stale
# voxel-era dirs stay (cleanup is a fresh-install concern, test 3b).
mkdir -p "$G/lovegame/red" "$G/conf"
echo 'save' > "$G/lovegame/red/cache.bin"
# "opts" is not the serializer's "return {" first line, so this also proves
# the index-seeding step leaves a file it can't parse alone (the game
# recovers such a file from its own .bak).
echo 'opts' > "$G/lovegame/options.lua"
# A foreign alias line (another catalog entry / a user rename) must survive
# the update's upsert, and our own line must not duplicate.
printf 'Other.sh\tOther Game\n' >> "$MAPFILE"
mkdir -p "$G/lovegame/mods/pokepcfollowers"
echo 'stale' > "$G/lovegame/mods/pokepcfollowers/manifest.json"
mkdir -p "$G/lovegame/mods/DRAMATIC_SHAPE"
echo 'stale' > "$G/lovegame/mods/DRAMATIC_SHAPE/manifest.json"
echo 'tweaked' > "$G/lovegame/mods/running_shoes/manifest.json"
rm -rf "$G/lovegame/mods/overworld_wild_spawns"
echo 'love-bin-v2' > "$FIX/gen1recomp/bin/love.aarch64"
(cd "$FIX" && zip -qr "$TMP/game.zip" Gen1recomp.sh gen1recomp)  # rebuild zip
if run_install > "$TMP/log2.txt" 2>&1; then pass "update exits 0"
else fail "update exited non-zero: $(tail -3 "$TMP/log2.txt")"; fi
[ "$(cat "$G/lovegame/red/cache.bin")" = "save" ] && pass "update: ROM cache preserved"    || fail "update: ROM cache clobbered"
[ "$(cat "$G/lovegame/options.lua")" = "opts" ]   && pass "update: options.lua preserved (unparseable file left alone)" || fail "update: options.lua clobbered"
[ "$(cat "$G/bin/love.aarch64")" = "love-bin-v2" ] && pass "update: engine payload updated" || fail "update: engine not updated"
[ "$(grep -c "^Gen1recomp\.sh$TAB" "$MAPFILE" 2>/dev/null)" = "1" ] \
                                                  && pass "update: alias upserted, not duplicated" || fail "update: alias duplicated or lost"
grep -q "^Other\.sh${TAB}Other Game\$" "$MAPFILE" && pass "update: foreign alias line kept" || fail "update: foreign alias line lost"
grep -q 'Update: bundled mods left as-is' "$TMP/log2.txt" \
                                                  && pass "update: mods-untouched notice printed" || fail "update: no mods-untouched notice"
! grep -q 'Latest dramaless shape release' "$TMP/log2.txt" \
                                                  && pass "update: no mod release resolved" || fail "update: a mod release was resolved"
! grep -q 'Downloading Dramaless Shape Mod' "$TMP/log2.txt" \
                                                  && pass "update: dramaless not downloaded" || fail "update: dramaless was downloaded"
! grep -q 'Downloading Wilds of Kanto' "$TMP/log2.txt" \
                                                  && pass "update: wilds not downloaded" || fail "update: wilds was downloaded"
[ -d "$G/lovegame/mods/pokepcfollowers" ] \
                                                  && pass "update: player mods dir untouched (pokepcfollowers kept)" || fail "update: mods dir was cleaned on update"
[ -d "$G/lovegame/mods/DRAMATIC_SHAPE" ] \
                                                  && pass "update: stale voxel dir untouched on update" || fail "update: stale voxel dir removed on update"
[ "$(cat "$G/lovegame/mods/running_shoes/manifest.json")" = "tweaked" ] \
                                                  && pass "update: in-place player mod edit kept" || fail "update: player mod edit overwritten"
[ ! -d "$G/lovegame/mods/overworld_wild_spawns" ] \
                                                  && pass "update: player-removed mod stays removed" || fail "update: player-removed mod reinstalled"
[ -f "$G/lovegame/mods/DRAMALESS_SHAPE/manifest.json" ] \
                                                  && pass "update: existing bundled mod left in place" || fail "update: existing bundled mod lost"
[ "$(cat "$G/.nx_addon_version" 2>/dev/null)" = "v9.9.9" ] \
                                                  && pass "update: version marker rewritten" || fail "update: version marker wrong"
# The update re-extracts the game payload (fresh stock RomImporter.lua), so
# both source patches must be re-applied on the update path too.
grep -q 'and PAD_SPEED or nxDpadSpeed(self, dt)' "$G/lovegame/src/import/RomImporter.lua" \
                                                  && pass "update: d-pad tuning re-applied to fresh payload" || fail "update: d-pad tuning lost"
grep -q '_nxWheelHold' "$G/lovegame/src/import/RomImporter.lua" \
                                                  && pass "update: X/Y scroll patch re-applied to fresh payload" || fail "update: X/Y scroll patch lost"

# ---- 2b. index seeding into an existing options.lua ---------------------
# These runs still carry a version record, so they take the update path -
# proving the seeding step runs on updates too (it is config, not a mod:
# it's what lets an existing install manage mods in-game). A game-written
# options.lua with the empty "modIndexes = {}," line gets the seed spliced
# in - after "return {", with the empty line dropped and every other key
# byte-for-byte intact.
cat > "$G/lovegame/options.lua" <<'EOF'
return {
  haptics = "light",
  modIndexes = {},
  zoom = 0,
}
EOF
if run_install > "$TMP/log2b.txt" 2>&1; then pass "reinstall over empty-index options.lua exits 0"
else fail "reinstall over empty-index options.lua exited non-zero: $(tail -3 "$TMP/log2b.txt")"; fi
grep -q 'feed = "https://bryanthaboi.github.io/gen1recomp-mod-index/data/index.json"' "$G/lovegame/options.lua" \
  && pass "seed spliced into existing options.lua" || fail "seed missing from existing options.lua"
! grep -q '^  modIndexes = {},$' "$G/lovegame/options.lua" \
  && pass "empty modIndexes line replaced" || fail "empty modIndexes line still present"
grep -q '^  haptics = "light",$' "$G/lovegame/options.lua" \
  && pass "other option keys untouched by the splice" || fail "splice damaged other option keys"
grep -q '^  zoom = 0,$' "$G/lovegame/options.lua" \
  && pass "keys after the spliced line untouched" || fail "splice damaged keys after the modIndexes line"
grep -q '^    DRAMALESS_SHAPE = false,$' "$G/lovegame/options.lua" \
  && pass "voxel-off seeded into existing options.lua" || fail "voxel-off missing from existing options.lua"

# A player-built index list (non-empty modIndexes) is theirs - reinstall
# must not edit it, even though our feed is absent from it. (The perf
# defaults are orthogonal and still seed into the same file; a fpsCap the
# player moved off the stock 60 must survive.)
cat > "$G/lovegame/options.lua" <<'EOF'
return {
  fpsCap = 30,
  modIndexes = {
    [1] = {
      feed = "https://example.org/data/index.json",
      label = "example.org/index",
    },
  },
}
EOF
if PLATFORM_OVERRIDE=tg5040 run_install > "$TMP/log2c.txt" 2>&1; then pass "reinstall over player-built index list exits 0"
else fail "reinstall over player-built index list exited non-zero: $(tail -3 "$TMP/log2c.txt")"; fi
grep -q 'feed = "https://example.org/data/index.json"' "$G/lovegame/options.lua" \
  && pass "player-built index row kept" || fail "player-built index row lost"
! grep -q 'bryanthaboi.github.io' "$G/lovegame/options.lua" \
  && pass "player-built index list not appended to" || fail "our index was forced into the player's list"
grep -q '^  fpsCap = 30,$' "$G/lovegame/options.lua" \
  && pass "player's non-stock fps cap kept" || fail "player's fps cap overwritten"
grep -q '^    DRAMALESS_SHAPE = {$' "$G/lovegame/options.lua" \
  && pass "perf defaults still seeded alongside player's index list" || fail "perf defaults missing"

# A DRAMALESS_SHAPE options bucket the player already touched is theirs -
# the mod-options seed must skip entirely (their values untouched, nothing
# added), while the stock-60 fpsCap still moves to 40.
cat > "$G/lovegame/options.lua" <<'EOF'
return {
  fpsCap = 60,
  mods = {
    STADIUM_BATTLE_FX = false,
  },
  modOptions = {
    DRAMALESS_SHAPE = {
      renderScale = 1,
    },
  },
}
EOF
if PLATFORM_OVERRIDE=tg5040 run_install > "$TMP/log2c2.txt" 2>&1; then pass "reinstall over player-set mod options exits 0"
else fail "reinstall over player-set mod options exited non-zero: $(tail -3 "$TMP/log2c2.txt")"; fi
grep -q 'renderScale = 1,' "$G/lovegame/options.lua" \
  && pass "player's mod option value kept" || fail "player's mod option value overwritten"
! grep -q 'renderDistanceSetting' "$G/lovegame/options.lua" \
  && pass "no seed keys forced into the player's bucket" || fail "seed keys forced into player's bucket"
grep -q '^  fpsCap = 40,$' "$G/lovegame/options.lua" \
  && pass "stock fps cap 60 moved to 40" || fail "stock fps cap not moved"
# A non-empty mods enablement table is the player's - no voxel-off forced in.
! grep -q 'DRAMALESS_SHAPE = false' "$G/lovegame/options.lua" \
  && pass "player-toggled mods table left alone" || fail "voxel-off forced into player's mods table"
grep -q '^    STADIUM_BATTLE_FX = false,$' "$G/lovegame/options.lua" \
  && pass "player's own mod toggle kept" || fail "player's mod toggle lost"

# Idempotent: an already-seeded file (the fresh-install one) is not touched
# again, so the row never duplicates.
cat > "$G/lovegame/options.lua" <<'EOF'
return {
  modIndexes = {
    [1] = {
      feed = "https://bryanthaboi.github.io/gen1recomp-mod-index/data/index.json",
      label = "bryanthaboi/gen1recomp-mod-index",
    },
  },
}
EOF
if run_install > "$TMP/log2d.txt" 2>&1; then pass "reinstall over already-seeded options.lua exits 0"
else fail "reinstall over already-seeded options.lua exited non-zero: $(tail -3 "$TMP/log2d.txt")"; fi
[ "$(grep -c 'gen1recomp-mod-index/data/index.json' "$G/lovegame/options.lua")" = "1" ] \
  && pass "already-seeded index not duplicated" || fail "index row duplicated on reinstall"

# ---- 3. digest mismatch aborts before touching the install ------------
rm -rf "$SD/Roms/Xtra Games (EXTRAS)"
if GAME_DIGEST="deadbeef" run_install > "$TMP/log3.txt" 2>&1; then
  fail "digest mismatch did not abort"
else pass "digest mismatch aborts"; fi
[ ! -e "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" ] \
  && pass "no launcher registered on failure" || fail "launcher registered despite failure"
grep -qi 'checksum' "$TMP/log3.txt" && pass "failure names the checksum" || fail "no checksum message"

# ---- 3b. fresh install over a stale target + renamed mod asset ----------
# Clearing BOTH version records makes the next run a fresh install again
# (the state a manual cleanup leaves behind), which must clean the
# voxel-era stale dirs its bundled DRAMALESS_SHAPE conflicts with. The
# dramaless release is served under a name the primary glob does not match,
# so the resolve only succeeds through the sole-zip fallback - the guard
# against the next upstream asset rename.
rm -f "$SD/.userdata/shared/xtras/gen1recomp.version" "$G/.nx_addon_version"
mkdir -p "$G/lovegame/mods/pokepcfollowers" "$G/lovegame/mods/DRAMATIC_SHAPE"
echo 'stale' > "$G/lovegame/mods/DRAMATIC_SHAPE/manifest.json"
if DRAMALESS_ASSET_NAME="mystery-voxel-1.0.zip" run_install > "$TMP/log3b.txt" 2>&1; then
  pass "fresh install with renamed mod asset exits 0"
else fail "fresh install with renamed mod asset exited non-zero: $(tail -3 "$TMP/log3b.txt")"; fi
grep -q 'Latest dramaless shape release: v2.0.2' "$TMP/log3b.txt" \
  && pass "renamed asset resolved via sole-zip fallback" || fail "renamed asset did not resolve"
[ -f "$G/lovegame/mods/DRAMALESS_SHAPE/manifest.json" ] \
  && pass "fresh install: dramaless installed from renamed asset" || fail "fresh install: dramaless missing"
[ ! -d "$G/lovegame/mods/DRAMATIC_SHAPE" ] \
  && pass "fresh install: stale DRAMATIC_SHAPE removed" || fail "fresh install: stale DRAMATIC_SHAPE still present"
[ ! -d "$G/lovegame/mods/pokepcfollowers" ] \
  && pass "fresh install: stale pokepcfollowers removed" || fail "fresh install: stale pokepcfollowers still present"
[ -f "$G/lovegame/mods/overworld_wild_spawns/manifest.json" ] \
  && pass "fresh install: wilds reinstalled" || fail "fresh install: wilds missing"

# ---- 4. failure after $TARGET is dirtied removes the stale launcher ---
# Start from a clean, successful install so a real launcher is registered
# (test 3 above tore the EXTRAS dir down and aborted before touching it).
rm -rf "$SD/Roms/Xtra Games (EXTRAS)"
if run_install > "$TMP/log4-setup.txt" 2>&1; then pass "setup: baseline install exits 0"
else fail "setup: baseline install exited non-zero: $(tail -3 "$TMP/log4-setup.txt")"; fi
[ -f "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" ] \
  && pass "setup: baseline launcher present" || fail "setup: baseline launcher missing"

# Force the FRESH path (mods are only fetched/extracted there) by clearing
# the version records the baseline just wrote, then corrupt the dramaless
# payload in place. run_install() recomputes each fixture digest from the
# CURRENT file each call, so the download/checksum step still passes and
# the failure instead happens during extraction - i.e. after the game
# payload overlay-copy has already started writing into $TARGET, which is
# exactly the scenario the fix covers.
rm -f "$SD/.userdata/shared/xtras/gen1recomp.version" "$G/.nx_addon_version"
echo 'not a zip file' > "$TMP/dramaless.zip"
if run_install > "$TMP/log4.txt" 2>&1; then
  fail "corrupt mod payload did not abort"
else pass "corrupt mod payload aborts"; fi
[ -f "$G/bin/love.aarch64" ] \
  && pass "target payload preserved despite mid-install failure" || fail "target payload missing after failed reinstall"
[ ! -e "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" ] \
  && pass "stale launcher removed after mid-install failure" || fail "stale launcher still present after mid-install failure"
grep -q 'install broken, launcher removed' "$TMP/log4.txt" \
  && pass "failure explains launcher removal" || fail "no launcher-removal explanation in log"

# ---- 5. ROM auto-scan works with NO sha1 tool anywhere on PATH ----------
# The original field gap: the scan was sha1-keyed, busybox on the card has no
# sha1sum applet and the only sha1sum lives inside PortMaster's vendored bin
# - so runtime=native users (no PortMaster) always got "install succeeded,
# 0 ROMs copied". The scan is sha256-keyed now (same busybox sha256sum the
# download digest checks already require), so it must WORK on a PATH with no
# sha1 tool at all. Prove that deterministically: an isolated PATH containing
# only the exact binaries install.sh needs (this test's own wget/sha256sum
# shims, plus the real system mkdir/rm/cp/basename/cut/unzip/dirname by
# direct symlink), and nothing else - a bare `PATH="$BIN:$PATH"` prepend
# wouldn't prove anything on a host that has a real sha1sum elsewhere on its
# inherited PATH (this dev machine does: /sbin/sha1sum).
BIN_NOSHA1="$TMP/bin_nosha1"
mkdir -p "$BIN_NOSHA1"
# shasum is what the sha256sum shim shells out to internally (macOS has no
# real sha256sum) - it lives in a different real directory (/usr/bin) than
# this host's real sha1sum (/sbin), so symlinking it by exact binary keeps
# the "no sha1sum anywhere on PATH" guarantee.
for c in bash mkdir rm cp mv basename cat cut unzip dirname shasum sed grep head; do
  tool_path="$(command -v "$c")" || { fail "host is missing '$c', can't build the no-sha1 PATH fixture"; tool_path=""; }
  [ -n "$tool_path" ] && ln -s "$tool_path" "$BIN_NOSHA1/$c"
done
ln -s "$BIN/wget" "$BIN_NOSHA1/wget"
ln -s "$BIN/sha256sum" "$BIN_NOSHA1/sha256sum"

# Test 4 left $TMP/dramaless.zip corrupted on purpose (not a valid archive);
# zip won't overwrite it in place ("Zip file structure invalid"), so remove
# it before rebuilding a valid one - needed so this install can reach the
# ROM-scan step instead of failing earlier at extract.
rm -f "$TMP/dramaless.zip"
(cd "$DRAMALESSFIX" && zip -qr "$TMP/dramaless.zip" manifest.json assets)

# Write the API fixtures with the CURRENT (unmodified) PATH/shasum, as
# separate statements before the PATH override below - the digest defaults
# inside them run `sha`, which must not resolve against the isolated
# (sha1-less, and shasum-less) PATH.
write_game_json "$(sha "$TMP/game.zip")"
write_dramaless_json "$(sha "$TMP/dramaless.zip")"
write_stadium_json "$(sha "$TMP/stadium.zip")"
write_shoes_json "$(sha "$TMP/shoes.zip")"
write_wilds_json "$(sha "$TMP/wilds.zip")"

# Clear the version records too, so this run exercises the FULL fresh path
# (mod downloads, mod installs and the index seeding, cat included) on the
# isolated PATH - not just the shorter update path.
rm -rf "$SD/Roms/Xtra Games (EXTRAS)" "$SD/.userdata/shared/xtras"
if PATH="$BIN_NOSHA1" \
   PLATFORM=tg5050 \
   SDCARD_PATH="$SD" \
   LOGS_PATH="$SD/.userdata/tg5050/logs" \
   EXTRAS_ROMS_DIR="$SD/Roms/Xtra Games (EXTRAS)" \
   EXTRAS_DATA_DIR="$SD/Roms/Xtra Games (EXTRAS)/.data" \
   CATALOG_DIR="$ENTRY" \
   XTRAS_STATE_DIR="$SD/.userdata/shared/xtras" \
   NX_EXTRAS_UNZIP=unzip \
   bash "$ENTRY/install.sh" > "$TMP/log5.txt" 2>&1
then pass "install with no sha1 tool anywhere exits 0"
else fail "install with no sha1 tool anywhere exited non-zero: $(tail -3 "$TMP/log5.txt")"; fi
[ -f "$G/lovegame/Pokemon Red.gb" ] \
  && pass "no sha1 tool: ROM scan still copied Red (sha256-keyed)" || fail "no sha1 tool: ROM scan copied nothing"
[ ! -f "$G/lovegame/Tetris.gb" ] \
  && pass "no sha1 tool: junk ROM still skipped" || fail "no sha1 tool: junk ROM copied"
! grep -qi 'ROM auto-scan skipped' "$TMP/log5.txt" \
  && pass "no sha1 tool: no skip message printed" || fail "no sha1 tool: scan still skipped itself"
[ -f "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" ] \
  && pass "no sha1 tool: install completed (launcher registered)" \
  || fail "no sha1 tool: install did not complete"
[ -f "$G/lovegame/mods/DRAMALESS_SHAPE/manifest.json" ] \
  && pass "no sha1 tool: fresh path ran (mods installed)" \
  || fail "no sha1 tool: fresh path did not install mods"
grep -q 'feed = "https://bryanthaboi.github.io/gen1recomp-mod-index/data/index.json"' "$G/lovegame/options.lua" \
  && pass "no sha1 tool: index seeded on isolated PATH" \
  || fail "no sha1 tool: index seeding failed on isolated PATH"

# ---- 6. uninstall keeps saves ------------------------------------------
# uninstall.sh's contract (Task 9): keep-saves - remove the launcher, the
# marker, and the engine/runtime payload, but never touch anything a play
# session would have written (ROM save caches, options.lua, conf/, the
# user's own ROM copies). Needs no network shims at all.
run_uninstall() {
  PATH="$BIN:$PATH" \
  PLATFORM=tg5050 \
  SDCARD_PATH="$SD" \
  LOGS_PATH="$SD/.userdata/tg5050/logs" \
  EXTRAS_ROMS_DIR="$SD/Roms/Xtra Games (EXTRAS)" \
  EXTRAS_DATA_DIR="$SD/Roms/Xtra Games (EXTRAS)/.data" \
  CATALOG_DIR="$ENTRY" \
  XTRAS_STATE_DIR="$SD/.userdata/shared/xtras" \
  bash "$ENTRY/uninstall.sh"
}

# Fresh baseline install (normal PATH, so the ROM auto-scan runs) - section 5
# above deliberately left the sandbox in a no-ROM state, which isn't a useful
# starting point for proving preserved user data survives uninstall.
rm -rf "$SD/Roms/Xtra Games (EXTRAS)"
if run_install > "$TMP/log6-setup.txt" 2>&1; then pass "setup: uninstall-test baseline install exits 0"
else fail "setup: uninstall-test baseline install exited non-zero: $(tail -3 "$TMP/log6-setup.txt")"; fi

# Simulate save data + config a real play session would have created -
# nothing install.sh itself writes, so nothing already asserts these exist.
mkdir -p "$G/lovegame/red" "$G/conf"
echo 'save' > "$G/lovegame/red/cache.bin"
echo 'opts' > "$G/lovegame/options.lua"
echo 'cfg'  > "$G/conf/settings.cfg"
# A foreign alias line: uninstall must drop only this entry's line.
printf 'Other.sh\tOther Game\n' >> "$MAPFILE"

if run_uninstall > "$TMP/log6.txt" 2>&1; then pass "uninstall exits 0"
else fail "uninstall exited non-zero: $(tail -3 "$TMP/log6.txt")"; fi

[ ! -e "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" ] \
  && pass "uninstall: launcher removed"        || fail "uninstall: launcher still present"
! grep -q "^Gen1recomp\.sh$TAB" "$MAPFILE" 2>/dev/null \
  && pass "uninstall: display alias removed"   || fail "uninstall: display alias still present"
grep -q "^Other\.sh${TAB}Other Game\$" "$MAPFILE" 2>/dev/null \
  && pass "uninstall: foreign alias line kept" || fail "uninstall: foreign alias line lost"
[ ! -e "$G/.nx_addon_version" ]                 \
  && pass "uninstall: version marker removed"  || fail "uninstall: version marker still present"
[ ! -e "$SD/.userdata/shared/xtras/gen1recomp.version" ] \
  && pass "uninstall: version record removed"  || fail "uninstall: version record still present"
[ ! -d "$G/bin" ]                               \
  && pass "uninstall: bin/ removed"             || fail "uninstall: bin/ still present"
[ ! -d "$G/libs.aarch64" ]                      \
  && pass "uninstall: libs.aarch64/ removed"    || fail "uninstall: libs.aarch64/ still present"
[ ! -f "$G/lovegame/main.lua" ]                 \
  && pass "uninstall: engine main.lua removed"  || fail "uninstall: main.lua still present"
[ ! -d "$G/lovegame/tools" ]                    \
  && pass "uninstall: lovegame/tools removed"   || fail "uninstall: lovegame/tools still present"
[ ! -d "$G/lovegame/mods" ]                     \
  && pass "uninstall: lovegame/mods removed"    || fail "uninstall: lovegame/mods still present"

[ "$(cat "$G/lovegame/red/cache.bin" 2>/dev/null)" = "save" ] \
  && pass "uninstall: ROM save cache kept"      || fail "uninstall: ROM save cache lost"
[ "$(cat "$G/lovegame/options.lua" 2>/dev/null)" = "opts" ]   \
  && pass "uninstall: options.lua kept"         || fail "uninstall: options.lua lost"
[ "$(cat "$G/conf/settings.cfg" 2>/dev/null)" = "cfg" ]       \
  && pass "uninstall: conf/ kept"               || fail "uninstall: conf/ lost"
[ -f "$G/lovegame/Pokemon Red.gb" ]             \
  && pass "uninstall: copied ROM kept"          || fail "uninstall: copied ROM lost"

grep -qi 'saves and ROMs kept' "$TMP/log6.txt" \
  && pass "uninstall: kept-message printed"     || fail "uninstall: kept-message missing"
# Task 11: uninstall.sh's own "@NN status text" progress hints.
grep -q '@100 ' "$TMP/log6.txt"                 \
  && pass "uninstall emits @NN progress hints"  || fail "no @NN progress hint found in uninstall output"

# Idempotent: nothing above still exists, so a second run must exit 0 too
# rather than erroring on already-missing paths.
if run_uninstall > "$TMP/log6b.txt" 2>&1; then pass "uninstall: second run exits 0 (idempotent)"
else fail "uninstall: second run exited non-zero: $(tail -3 "$TMP/log6b.txt")"; fi
[ "$(cat "$G/lovegame/red/cache.bin" 2>/dev/null)" = "save" ] \
  && pass "uninstall: idempotent run didn't touch saves either" || fail "uninstall: second run touched saves"

# ---- 7. reinstall after uninstall fully restores the entry -------------
# uninstall.sh cleared BOTH version records, so this run is a fresh install
# again and must restore the bundled mods, not just the engine.
if run_install > "$TMP/log7.txt" 2>&1; then pass "reinstall after uninstall exits 0"
else fail "reinstall after uninstall exited non-zero: $(tail -3 "$TMP/log7.txt")"; fi
[ -f "$G/bin/love.aarch64" ] \
  && pass "reinstall after uninstall: engine restored"   || fail "reinstall after uninstall: engine missing"
[ -f "$G/lovegame/mods/DRAMALESS_SHAPE/manifest.json" ] && [ -f "$G/lovegame/mods/overworld_wild_spawns/manifest.json" ] \
  && pass "reinstall after uninstall: bundled mods restored (fresh path)" || fail "reinstall after uninstall: bundled mods missing"
[ -f "$SD/Roms/Xtra Games (EXTRAS)/Gen1recomp.sh" ] \
  && pass "reinstall after uninstall: launcher restored" || fail "reinstall after uninstall: launcher missing"
grep -q "^Gen1recomp\.sh${TAB}Gen1Recomp (Pokemon R/B/Y)\$" "$MAPFILE" 2>/dev/null \
  && pass "reinstall after uninstall: display alias restored" || fail "reinstall after uninstall: display alias missing"
[ "$(cat "$G/.nx_addon_version" 2>/dev/null)" = "v9.9.9" ] \
  && pass "reinstall after uninstall: version marker restored" || fail "reinstall after uninstall: version marker missing"
[ "$(cat "$G/lovegame/red/cache.bin" 2>/dev/null)" = "save" ] \
  && pass "reinstall after uninstall: save cache still kept"   || fail "reinstall after uninstall: save cache lost"

say ""
if [ "$FAILS" -eq 0 ]; then say "ALL PASS"; exit 0; else say "$FAILS FAILURE(S)"; exit 1; fi
