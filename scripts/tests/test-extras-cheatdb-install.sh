#!/usr/bin/env bash
# Host test for the THIN cheatdb Xtras installer: it copies the pak payload
# into Tools/ and busts the list caches; it must NOT download or extract.
# shellcheck disable=SC2015,SC2016
set -u
FAILS=0
say()  { printf '%s\n' "$*"; }
pass() { say "PASS: $*"; }
fail() { say "FAIL: $*"; FAILS=$((FAILS+1)); }

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENTRY="$ROOT/skeleton/SYSTEM/tg5050/paks/Tools/Xtras.pak/catalog/cheatdb"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
SD="$TMP/sd"
mkdir -p "$SD/.userdata/tg5050" "$SD/Cheats/GBA"
echo 'user cheat' > "$SD/Cheats/GBA/MyHomebrew.cht"
# seed a fake pack manifest + data so uninstall has something to remove
mkdir -p "$SD/.userdata/shared/xtras"
echo 'pack cheat' > "$SD/Cheats/GBA/Advance Wars (USA, Europe) (Code Breaker).cht"
printf '%s\n' "$SD/Cheats/GBA/Advance Wars (USA, Europe) (Code Breaker).cht" > "$SD/.userdata/shared/xtras/cheatdb.manifest"
echo '2026-09-14' > "$SD/.userdata/shared/xtras/cheatdb.db_lastmod"

run_install() {
  PLATFORM=tg5050 SDCARD_PATH="$SD" LOGS_PATH="$SD/.userdata/tg5050" \
  CATALOG_DIR="$ENTRY" XTRAS_STATE_DIR="$SD/.userdata/shared/xtras" \
  bash "$ENTRY/install.sh"
}
run_uninstall() {
  PLATFORM=tg5050 SDCARD_PATH="$SD" LOGS_PATH="$SD/.userdata/tg5050" \
  CATALOG_DIR="$ENTRY" XTRAS_STATE_DIR="$SD/.userdata/shared/xtras" \
  bash "$ENTRY/uninstall.sh"
}
PAK="$SD/Tools/Cheat Database.pak"

# ---- 1. thin install copies the pak, no download ----
if run_install > "$TMP/log1.txt" 2>&1; then pass "install exits 0"
else fail "install exited non-zero: $(tail -3 "$TMP/log1.txt")"; fi
[ -x "$PAK/launch.sh" ] && pass "pak launch.sh installed + executable" || fail "pak launch.sh missing"
! grep -qi 'buildbot\|Downloading' "$TMP/log1.txt" && pass "install did not download" || fail "install attempted a download"
[ ! -e "$SD/.userdata/tg5050/emulist_cache.txt" ] && pass "emulist cache busted" || fail "emulist cache not busted"
[ ! -f "$SD/.userdata/shared/xtras/cheatdb.version" ] && pass "install did not write the version marker (extras.c owns it)" || fail "install wrote the marker"

# ---- 2. uninstall removes pak + data, keeps hand-made ----
if run_uninstall > "$TMP/log2.txt" 2>&1; then pass "uninstall exits 0"
else fail "uninstall exited non-zero: $(tail -3 "$TMP/log2.txt")"; fi
[ ! -d "$PAK" ] && pass "uninstall removed the pak" || fail "pak still present"
[ ! -e "$SD/Cheats/GBA/Advance Wars (USA, Europe) (Code Breaker).cht" ] && pass "uninstall removed pack data" || fail "pack data left"
[ -f "$SD/Cheats/GBA/MyHomebrew.cht" ] && pass "uninstall kept hand-made cheat" || fail "hand-made cheat removed"
[ ! -e "$SD/.userdata/shared/xtras/cheatdb.manifest" ] && pass "uninstall removed manifest" || fail "manifest left"
[ ! -e "$SD/.userdata/shared/xtras/cheatdb.db_lastmod" ] && pass "uninstall removed db_lastmod" || fail "db_lastmod left"

say ""; if [ "$FAILS" -eq 0 ]; then say "ALL PASS"; exit 0; else say "$FAILS FAILURE(S)"; exit 1; fi
