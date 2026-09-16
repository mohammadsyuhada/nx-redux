#!/usr/bin/env bash
# Host-side test for the psp (PPSSPP) Xtras catalog installer.
# Network and device binaries are shimmed via PATH; all paths are sandboxed.
# A && pass "..." || fail "..." is used throughout below: pass()/fail() only
# printf and never fail, so the || branch never runs when the check is true.
# shellcheck disable=SC2015
# Single quotes are deliberate in the grep pattern below: it must match the
# literal $PLATFORM/$PAK_NAME text inside the installed launch.sh, not
# expand against this script's own variables.
# shellcheck disable=SC2016
set -u

FAILS=0
say()  { printf '%s\n' "$*"; }
pass() { say "PASS: $*"; }
fail() { say "FAIL: $*"; FAILS=$((FAILS+1)); }

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENTRY="$ROOT/skeleton/SYSTEM/tg5050/paks/Tools/Xtras.pak/catalog/psp"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---- fixtures ---------------------------------------------------------
# Fake upstream PSP.pak.zip: flat layout (launch.sh/PPSSPP/bin at the
# archive root, no wrapper dir), with upstream's platform-hardcoded
# launch.sh so the "installed verbatim" assertion below is meaningful.
FIX="$TMP/fix"
mkdir -p "$FIX/PPSSPP/.config/ppsspp/PSP/SYSTEM" "$FIX/bin"
cat > "$FIX/launch.sh" <<'EOF'
#!/bin/sh
PAK_DIR="$(dirname "$0")"
export PAK_DIR="$SDCARD_PATH/Emus/$PLATFORM/$PAK_NAME.pak"
EOF
echo 'ppsspp-tg5040-v1' > "$FIX/PPSSPP/PPSSPPSDL_tg5040"
echo 'ppsspp-tg5050-v1' > "$FIX/PPSSPP/PPSSPPSDL_tg5050"
echo 'stock-ini'        > "$FIX/PPSSPP/.config/ppsspp/PSP/SYSTEM/ppsspp.ini"
echo 'power-control'    > "$FIX/bin/minui-power-control"
echo 'setalpha'         > "$FIX/bin/setalpha"
echo '{}'               > "$FIX/pak.json"
(cd "$FIX" && zip -qr "$TMP/pak.zip" launch.sh PPSSPP bin pak.json)

sha() { shasum -a 256 "$1" | cut -d' ' -f1; }

# Fake GitHub latest-release API response. A decoy asset sits FIRST so the
# "matched by glob, paired with its own digest" logic is actually exercised
# (a naive first-URL/first-digest scrape would pick the decoy). Field order
# inside each asset (name -> digest -> browser_download_url) mirrors the
# real API - resolve_latest's token-stream walk depends on it.
write_release_json() { # tag digest [asset-name]
  cat > "$TMP/release.json" <<EOF
{
  "tag_name": "$1",
  "name": "Release $1",
  "assets": [
    {
      "name": "decoy-notes.txt",
      "digest": "sha256:1111111111111111111111111111111111111111111111111111111111111111",
      "browser_download_url": "https://github.com/ben16w/minui-psp/releases/download/$1/decoy-notes.txt"
    },
    {
      "name": "${3:-PSP.pak.zip}",
      "digest": "sha256:$2",
      "browser_download_url": "https://github.com/ben16w/minui-psp/releases/download/$1/${3:-PSP.pak.zip}"
    }
  ]
}
EOF
}

# ---- sandbox SD card --------------------------------------------------
SD="$TMP/sd"
mkdir -p "$SD/.userdata/tg5050/logs"

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
  *releases/latest) cp "$TMP/release.json" "\$out" ;;
  *PSP.pak.zip) cp "$TMP/pak.zip" "\$out" ;;
  *) echo "wget shim: unknown url \$url" >&2; exit 1 ;;
esac
SHIM
chmod +x "$BIN/wget"
# sha256sum shim -> host shasum (macOS has no sha256sum).
cat > "$BIN/sha256sum" <<'SHIM'
#!/usr/bin/env bash
shasum -a 256 "$@"
SHIM
chmod +x "$BIN/sha256sum"

run_install() {
  # Regenerate the API fixture each call: the digest defaults to the CURRENT
  # pak.zip's real hash (test 3 rebuilds the zip), and the FIX_* overrides
  # let individual scenarios serve a bad digest / different asset name / an
  # empty tag without touching the happy-path plumbing.
  # ${FIX_TAG-...} (no colon): scenario 4c overrides with an EMPTY tag, which
  # ":-" would silently replace with the default.
  write_release_json "${FIX_TAG-9.9.9}" "${FIX_DIGEST:-$(sha "$TMP/pak.zip")}" "${FIX_ASSET:-PSP.pak.zip}"
  PATH="$BIN:$PATH" \
  PLATFORM=tg5050 \
  SDCARD_PATH="$SD" \
  LOGS_PATH="$SD/.userdata/tg5050/logs" \
  EXTRAS_ROMS_DIR="$SD/Roms/Xtra Games (EXTRAS)" \
  EXTRAS_PORTS_DIR="$SD/Roms/Xtra Games (EXTRAS)/.ports" \
  CATALOG_DIR="$ENTRY" \
  XTRAS_STATE_DIR="$SD/.userdata/shared/xtras" \
  NX_EXTRAS_UNZIP="${NX_EXTRAS_UNZIP:-unzip}" \
  bash "$ENTRY/install.sh"
}

run_uninstall() {
  PATH="$BIN:$PATH" \
  PLATFORM=tg5050 \
  SDCARD_PATH="$SD" \
  LOGS_PATH="$SD/.userdata/tg5050/logs" \
  EXTRAS_ROMS_DIR="$SD/Roms/Xtra Games (EXTRAS)" \
  EXTRAS_PORTS_DIR="$SD/Roms/Xtra Games (EXTRAS)/.ports" \
  CATALOG_DIR="$ENTRY" \
  XTRAS_STATE_DIR="$SD/.userdata/shared/xtras" \
  bash "$ENTRY/uninstall.sh"
}

PAK="$SD/Emus/tg5050/PSP.pak"   # upstream's own Emus/$PLATFORM/ layout
OLD="$SD/Emus/PSP.pak"          # legacy flat location (installs before 2026-09-16)
INI="PPSSPP/.config/ppsspp/PSP/SYSTEM/ppsspp.ini"
VER="$SD/.userdata/shared/xtras/psp.version"

# ---- 0. preflight: unzip tool must resolve before any download --------
# psp deliberately depends on the SYSTEM-shipped 7zzs (the updater's own
# extractor), never PortMaster's - unlike gen1recomp it has no PortMaster
# launch-time dependency, so install must work on a PortMaster-less card.
if NX_EXTRAS_UNZIP="$SD/.system/shared/bin/7zzs.aarch64" \
   run_install > "$TMP/log0.txt" 2>&1; then
  fail "preflight: missing unzip tool did not abort"
else pass "preflight: missing unzip tool aborts"; fi
grep -q 'system unzip tool missing' "$TMP/log0.txt" \
  && pass "preflight: message printed" || fail "preflight: message missing"
! grep -q 'Emus/shared/PortMaster' "$ENTRY/install.sh" \
  && pass "install.sh has no PortMaster path dependency" || fail "install.sh still references a PortMaster path"
! grep -q 'Downloading' "$TMP/log0.txt" \
  && pass "preflight: no download attempted" || fail "preflight: a download was attempted despite missing unzip tool"
[ ! -d "$PAK" ] \
  && pass "preflight: nothing written to install target" || fail "preflight: install target created despite missing unzip tool"

# ---- 1. fresh install --------------------------------------------------
if run_install > "$TMP/log1.txt" 2>&1; then pass "fresh install exits 0"
else fail "fresh install exited non-zero: $(tail -3 "$TMP/log1.txt")"; fi
[ -f "$PAK/PPSSPP/PPSSPPSDL_tg5050" ] && pass "emulator payload extracted"  || fail "emulator payload missing"
[ -f "$PAK/bin/minui-power-control" ] && pass "bin/ payload extracted"      || fail "bin/ payload missing"
[ -f "$PAK/$INI" ]                    && pass "stock config extracted"      || fail "stock config missing"
# The whole point of the layout (2026-09-16): the pak lands in upstream's
# own Emus/$PLATFORM/ location and its launch.sh is upstream's, byte for
# byte. Our earlier flat-location install swapped in a catalog copy of
# launch.sh, which silently fell behind upstream (minui-psp 6.x added the
# Brick "setalpha 0" display fix; our copy lacked it -> black screen).
# The catalog must ship NO launch.sh at all, so nothing can drift again.
cmp -s "$PAK/launch.sh" "$FIX/launch.sh" \
  && pass "launch.sh installed verbatim from upstream" || fail "launch.sh differs from upstream's"
grep -q 'Emus/\$PLATFORM/\$PAK_NAME' "$PAK/launch.sh" \
  && pass "installed launch.sh is upstream's platform-path one" || fail "upstream's platform-path launch.sh was altered"
[ ! -e "$ENTRY/launch.sh" ] && [ ! -e "$ROOT/skeleton/SYSTEM/tg5040/paks/Tools/Xtras.pak/catalog/psp/launch.sh" ] \
  && pass "catalog ships no launch.sh override" || fail "a catalog launch.sh override still exists"
[ -x "$PAK/launch.sh" ] && [ -x "$PAK/PPSSPP/PPSSPPSDL_tg5050" ] \
  && pass "exec bits set on launcher and binary" || fail "exec bits missing"
[ -x "$PAK/bin/setalpha" ] \
  && pass "exec bit set on bin/setalpha" || fail "exec bit missing on bin/setalpha"
[ ! -e "$OLD" ] \
  && pass "nothing written to the legacy flat location" || fail "legacy flat location was written"
[ -d "$SD/Roms/Sony Playstation Portable (PSP)" ] \
  && pass "PSP Roms folder created" || fail "PSP Roms folder missing"
[ "$(cat "$VER" 2>/dev/null)" = "9.9.9" ] \
  && pass "resolved tag written to version record" || fail "version record wrong: '$(cat "$VER" 2>/dev/null)'"
[ ! -e "$SD/Roms/Xtra Games (EXTRAS)/.ports/psp" ] \
  && pass "no legacy marker dir created" || fail "legacy marker dir was created"
grep -q '@70 ' "$TMP/log1.txt" \
  && pass "install emits @NN progress hints" || fail "no @NN progress hint found in install output"
grep -q 'Latest release: 9.9.9' "$TMP/log1.txt" \
  && pass "resolved release announced in output" || fail "no resolved-release line in output"

# ---- 2. migration: legacy flat Emus/PSP.pak is absorbed -----------------
# nextui probes the FLAT location first, so a leftover flat copy would
# shadow the new install - migration must remove it.
# 2a. fresh install (no platform pak yet) + old flat install with user
# config -> config carried over, old flat copy removed.
rm -rf "$SD/Emus" "$SD/Roms/Xtra Games (EXTRAS)"
mkdir -p "$OLD/PPSSPP/.config/ppsspp/PSP/SYSTEM" "$OLD/PPSSPP/.config/ppsspp/PSP/TEXTURES"
echo 'user-tweaked-ini' > "$OLD/$INI"
echo 'texture-pack'     > "$OLD/PPSSPP/.config/ppsspp/PSP/TEXTURES/pack.zip"
echo 'old-launcher'     > "$OLD/launch.sh"
if run_install > "$TMP/log2a.txt" 2>&1; then pass "migration install exits 0"
else fail "migration install exited non-zero: $(tail -3 "$TMP/log2a.txt")"; fi
[ "$(cat "$PAK/$INI" 2>/dev/null)" = "user-tweaked-ini" ] \
  && pass "migration: old config carried over" || fail "migration: old config lost"
[ -f "$PAK/PPSSPP/.config/ppsspp/PSP/TEXTURES/pack.zip" ] \
  && pass "migration: texture pack carried over" || fail "migration: texture pack lost"
[ ! -d "$OLD" ] \
  && pass "migration: old flat install removed" || fail "migration: old flat install still present"
[ -x "$PAK/launch.sh" ] \
  && pass "migration: pak now at the platform location" || fail "migration: pak missing from the platform location"

# 2b. Emus/ holding ANOTHER user pak too -> only the flat PSP.pak goes.
rm -rf "$SD/Emus" "$SD/Roms/Xtra Games (EXTRAS)"
mkdir -p "$OLD" "$SD/Emus/N64.pak"
echo 'old' > "$OLD/launch.sh"
echo 'n64' > "$SD/Emus/N64.pak/launch.sh"
if run_install > "$TMP/log2b.txt" 2>&1; then pass "migration (other paks present) exits 0"
else fail "migration (other paks present) exited non-zero: $(tail -3 "$TMP/log2b.txt")"; fi
[ ! -d "$OLD" ] \
  && pass "migration: flat PSP.pak removed" || fail "migration: old flat PSP.pak still present"
[ -f "$SD/Emus/N64.pak/launch.sh" ] \
  && pass "migration: unrelated pak left alone" || fail "migration: unrelated pak damaged"

# 2c. a hand-installed community pak already at the platform location is
# simply upgraded in place, keeping its config.
rm -rf "$SD/Emus" "$SD/Roms/Xtra Games (EXTRAS)"
mkdir -p "$PAK/PPSSPP/.config/ppsspp/PSP/SYSTEM"
echo 'hand-installed-ini' > "$PAK/$INI"
echo 'hand-launcher'      > "$PAK/launch.sh"
if run_install > "$TMP/log2c.txt" 2>&1; then pass "upgrade of hand-installed pak exits 0"
else fail "upgrade of hand-installed pak exited non-zero: $(tail -3 "$TMP/log2c.txt")"; fi
[ "$(cat "$PAK/$INI" 2>/dev/null)" = "hand-installed-ini" ] \
  && pass "upgrade in place: config kept" || fail "upgrade in place: config clobbered"
cmp -s "$PAK/launch.sh" "$FIX/launch.sh" \
  && pass "upgrade in place: launch.sh refreshed from upstream" || fail "upgrade in place: launch.sh not refreshed"

# ---- 3. reinstall preserves user config --------------------------------
echo 'user-edited-ini' > "$PAK/$INI"
mkdir -p "$PAK/PPSSPP/.config/ppsspp/PSP/TEXTURES"
echo 'my-textures' > "$PAK/PPSSPP/.config/ppsspp/PSP/TEXTURES/mine.zip"
echo 'ppsspp-tg5050-v2' > "$FIX/PPSSPP/PPSSPPSDL_tg5050"
rm -f "$TMP/pak.zip"
(cd "$FIX" && zip -qr "$TMP/pak.zip" launch.sh PPSSPP bin pak.json)  # rebuild zip
if run_install > "$TMP/log3.txt" 2>&1; then pass "reinstall exits 0"
else fail "reinstall exited non-zero: $(tail -3 "$TMP/log3.txt")"; fi
[ "$(cat "$PAK/$INI")" = "user-edited-ini" ] \
  && pass "reinstall: user config preserved" || fail "reinstall: user config clobbered"
[ -f "$PAK/PPSSPP/.config/ppsspp/PSP/TEXTURES/mine.zip" ] \
  && pass "reinstall: user textures preserved" || fail "reinstall: user textures lost"
[ "$(cat "$PAK/PPSSPP/PPSSPPSDL_tg5050")" = "ppsspp-tg5050-v2" ] \
  && pass "reinstall: emulator payload updated" || fail "reinstall: emulator not updated"

# ---- 4. bad API data aborts before touching the install ----------------
# 4a. digest mismatch (corrupted download or tampered API response)
rm -rf "$SD/Emus" "$SD/Roms/Xtra Games (EXTRAS)" "$SD/.userdata/shared/xtras"
if FIX_DIGEST="deadbeef" run_install > "$TMP/log4.txt" 2>&1; then
  fail "digest mismatch did not abort"
else pass "digest mismatch aborts"; fi
[ ! -d "$PAK" ] \
  && pass "nothing installed on digest failure" || fail "install target created despite digest failure"
grep -qi 'checksum' "$TMP/log4.txt" && pass "failure names the checksum" || fail "no checksum message"
[ ! -e "$VER" ] && pass "no version record on digest failure" || fail "version record written despite failure"

# 4b. latest release has no asset matching the meta glob
if FIX_ASSET="PSP.pak.7z" run_install > "$TMP/log4b.txt" 2>&1; then
  fail "missing matching asset did not abort"
else pass "missing matching asset aborts"; fi
grep -q 'no matching download' "$TMP/log4b.txt" \
  && pass "failure names the missing asset" || fail "no missing-asset message"

# 4c. unparseable release info (empty tag_name)
if FIX_TAG="" run_install > "$TMP/log4c.txt" 2>&1; then
  fail "empty tag did not abort"
else pass "empty tag aborts"; fi
grep -q 'could not read the latest version info' "$TMP/log4c.txt" \
  && pass "failure names the version-info parse" || fail "no version-info message"

# ---- 5. failure after $TARGET is dirtied hides the broken pak ----------
# Baseline install, then block the version-record mkdir (make the state dir
# path a file) so the failure lands AFTER the overlay-copy dirtied the
# target - fail() must pull the pak's launch.sh so a half-written emulator
# is never left launchable (getEmuPath()/hasEmu() probe exactly that file).
if run_install > "$TMP/log5-setup.txt" 2>&1; then pass "setup: baseline install exits 0"
else fail "setup: baseline install exited non-zero: $(tail -3 "$TMP/log5-setup.txt")"; fi
rm -rf "$SD/.userdata/shared/xtras"
touch "$SD/.userdata/shared/xtras"
if run_install > "$TMP/log5.txt" 2>&1; then
  fail "blocked marker dir did not abort"
else pass "blocked marker dir aborts"; fi
[ ! -e "$PAK/launch.sh" ] \
  && pass "broken install: pak launcher removed" || fail "broken install: pak launcher still present"
[ -f "$PAK/PPSSPP/PPSSPPSDL_tg5050" ] \
  && pass "broken install: payload kept for repair-by-reinstall" || fail "broken install: payload missing"
grep -q 'install broken, pak launcher removed' "$TMP/log5.txt" \
  && pass "failure explains launcher removal" || fail "no launcher-removal explanation in log"
rm -f "$SD/.userdata/shared/xtras"

# ---- 6. uninstall keeps saves and settings ------------------------------
if run_install > "$TMP/log6-setup.txt" 2>&1; then pass "setup: uninstall-test baseline install exits 0"
else fail "setup: uninstall-test baseline install exited non-zero: $(tail -3 "$TMP/log6-setup.txt")"; fi
# Simulate what play sessions create: saves OUTSIDE the pak (bind-mount
# target), user config inside it, plus a lingering legacy flat copy.
mkdir -p "$SD/Saves/PSP" "$SD/Roms/Sony Playstation Portable (PSP)" "$OLD"
echo 'save-data' > "$SD/Saves/PSP/game.sav"
echo 'my-game'   > "$SD/Roms/Sony Playstation Portable (PSP)/game.iso"
echo 'user-ini'  > "$PAK/$INI"
echo 'leftover'  > "$OLD/launch.sh"

if run_uninstall > "$TMP/log6.txt" 2>&1; then pass "uninstall exits 0"
else fail "uninstall exited non-zero: $(tail -3 "$TMP/log6.txt")"; fi
[ ! -e "$PAK/launch.sh" ]                 && pass "uninstall: launcher removed"       || fail "uninstall: launcher still present"
[ ! -e "$VER" ]                           && pass "uninstall: version record removed" || fail "uninstall: version record still present"
[ ! -e "$PAK/PPSSPP/PPSSPPSDL_tg5050" ]   && pass "uninstall: emulator binaries removed" || fail "uninstall: emulator binaries still present"
[ ! -d "$PAK/bin" ]                       && pass "uninstall: bin/ removed"           || fail "uninstall: bin/ still present"
[ ! -d "$OLD" ]                           && pass "uninstall: legacy flat copy removed" || fail "uninstall: legacy flat copy still present"
[ "$(cat "$PAK/$INI" 2>/dev/null)" = "user-ini" ] \
  && pass "uninstall: user config kept"    || fail "uninstall: user config lost"
[ "$(cat "$SD/Saves/PSP/game.sav" 2>/dev/null)" = "save-data" ] \
  && pass "uninstall: saves kept"          || fail "uninstall: saves lost"
[ -f "$SD/Roms/Sony Playstation Portable (PSP)/game.iso" ] \
  && pass "uninstall: games kept"          || fail "uninstall: games lost"
grep -qi 'saves and ROMs kept' "$TMP/log6.txt" \
  && pass "uninstall: kept-message printed" || fail "uninstall: kept-message missing"
grep -q '@100 ' "$TMP/log6.txt" \
  && pass "uninstall emits @NN progress hints" || fail "no @NN progress hint found in uninstall output"

# Idempotent: a second run must exit 0 rather than erroring on
# already-missing paths, and must still not touch the kept data.
if run_uninstall > "$TMP/log6b.txt" 2>&1; then pass "uninstall: second run exits 0 (idempotent)"
else fail "uninstall: second run exited non-zero: $(tail -3 "$TMP/log6b.txt")"; fi
[ "$(cat "$PAK/$INI" 2>/dev/null)" = "user-ini" ] \
  && pass "uninstall: idempotent run didn't touch kept config" || fail "uninstall: second run touched kept config"

# When NO user config exists the empty pak shell goes too.
rm -rf "$PAK"
mkdir -p "$PAK/bin"
echo 'x' > "$PAK/launch.sh"
if run_uninstall > "$TMP/log6c.txt" 2>&1; then pass "uninstall (no user config) exits 0"
else fail "uninstall (no user config) exited non-zero: $(tail -3 "$TMP/log6c.txt")"; fi
[ ! -d "$PAK" ] \
  && pass "uninstall: config-less pak removed entirely" || fail "uninstall: empty pak shell left behind"
[ ! -d "$SD/Emus/tg5050" ] \
  && pass "uninstall: empty platform folder removed" || fail "uninstall: empty platform folder left behind"

# ...but a platform folder holding another pak stays.
mkdir -p "$PAK/bin" "$SD/Emus/tg5050/N64.pak"
echo 'x'   > "$PAK/launch.sh"
echo 'n64' > "$SD/Emus/tg5050/N64.pak/launch.sh"
if run_uninstall > "$TMP/log6d.txt" 2>&1; then pass "uninstall (sibling pak) exits 0"
else fail "uninstall (sibling pak) exited non-zero: $(tail -3 "$TMP/log6d.txt")"; fi
[ ! -d "$PAK" ] && [ -f "$SD/Emus/tg5050/N64.pak/launch.sh" ] \
  && pass "uninstall: sibling pak and its platform folder kept" || fail "uninstall: sibling pak damaged"
rm -rf "$SD/Emus/tg5050/N64.pak"

# ---- 7. reinstall after uninstall fully restores the entry --------------
if run_install > "$TMP/log7.txt" 2>&1; then pass "reinstall after uninstall exits 0"
else fail "reinstall after uninstall exited non-zero: $(tail -3 "$TMP/log7.txt")"; fi
[ -x "$PAK/launch.sh" ] \
  && pass "reinstall after uninstall: launcher restored" || fail "reinstall after uninstall: launcher missing"
[ "$(cat "$VER" 2>/dev/null)" = "9.9.9" ] \
  && pass "reinstall after uninstall: version record restored" || fail "reinstall after uninstall: version record missing"

say ""
if [ "$FAILS" -eq 0 ]; then say "ALL PASS"; exit 0; else say "$FAILS FAILURE(S)"; exit 1; fi
