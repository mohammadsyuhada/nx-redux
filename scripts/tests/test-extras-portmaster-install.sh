#!/usr/bin/env bash
# Host-side test for the portmaster Xtras catalog installer/uninstaller.
# Network and device binaries are shimmed via PATH; all paths are sandboxed.
# The catalog entry is COPIED into the sandbox first: pak/portmaster.elf is
# a build-time artifact (Makefile drops it into the catalog's pak/ dir), so
# the copy gets a stand-in elf without dirtying the repo skeleton.
# A && pass "..." || fail "..." is used throughout below: pass()/fail() only
# printf and never fail, so the || branch never runs when the check is true.
# shellcheck disable=SC2015
set -u

FAILS=0
say()  { printf '%s\n' "$*"; }
pass() { say "PASS: $*"; }
fail() { say "FAIL: $*"; FAILS=$((FAILS+1)); }

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REPO_ENTRY="$ROOT/skeleton/SYSTEM/tg5050/paks/Tools/Xtras.pak/catalog/portmaster"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---- catalog copies must not drift between platforms -------------------
for f in meta.txt install.sh uninstall.sh; do
  cmp -s "$ROOT/skeleton/SYSTEM/tg5040/paks/Tools/Xtras.pak/catalog/portmaster/$f" "$REPO_ENTRY/$f" \
    && pass "tg5040/tg5050 $f in sync" || fail "tg5040/tg5050 $f differ"
done

# ---- sandboxed catalog entry (repo copy + stand-in elf) -----------------
ENTRY="$TMP/entry"
cp -R "$REPO_ENTRY" "$ENTRY"
echo 'fake-elf' > "$ENTRY/pak/portmaster.elf"

# ---- fixtures -----------------------------------------------------------
# Fake upstream PortMaster.zip: PortMaster/ at the archive root (upstream's
# real layout), pugwash inside - the marker install.sh verifies.
FIX="$TMP/fix"
mkdir -p "$FIX/PortMaster"
echo 'pugwash-gui' > "$FIX/PortMaster/pugwash"
(cd "$FIX" && zip -qr "$TMP/PortMaster.zip" PortMaster)

# Bundled runtime deps, as skeleton BASE ships them on every card in
# Emus/shared/PortMaster/files/. The busybox stand-in answers --list so the
# wrapper-generation block actually runs on the host; gzip is a "real"
# pre-existing binary the wrapper pass must NOT clobber.
DEPS="$TMP/deps"
mkdir -p "$DEPS/bin" "$DEPS/lib" "$DEPS/libs" "$DEPS/py/pylibs/harbourmaster" "$DEPS/py/exlibs"
cat > "$DEPS/bin/busybox" <<'EOF'
#!/usr/bin/env bash
[ "$1" = "--list" ] && printf 'ls\nsh\ngzip\n'
EOF
echo 'real-gzip-binary'  > "$DEPS/bin/gzip"
echo 'lib-payload'       > "$DEPS/lib/libfoo.so.1"
echo 'gpu-driver'        > "$DEPS/lib/libEGL.so.1"
echo 'newer-glib'        > "$DEPS/lib/libglib-2.0.so.0"
echo 'libs-payload'      > "$DEPS/libs/marker-libs.txt"
echo 'hm-module'         > "$DEPS/py/pylibs/harbourmaster/marker.py"
echo 'exlib'             > "$DEPS/py/exlibs/marker.txt"
(cd "$DEPS/bin"  && zip -qr "$TMP/bin.zip" .)
(cd "$DEPS/lib"  && zip -qr "$TMP/lib.zip" .)
(cd "$DEPS/libs" && zip -qr "$TMP/libs.zip" .)
(cd "$DEPS/py"   && zip -qr "$TMP/pylibs.zip" pylibs exlibs)

sha() { shasum -a 256 "$1" | cut -d' ' -f1; }

# Fake GitHub latest-release API response. A decoy asset sits FIRST so the
# "matched by name, paired with its own digest" logic is actually exercised.
write_release_json() { # tag digest [asset-name]
  cat > "$TMP/release.json" <<EOF
{
  "tag_name": "$1",
  "name": "Release $1",
  "assets": [
    {
      "name": "decoy-notes.txt",
      "digest": "sha256:1111111111111111111111111111111111111111111111111111111111111111",
      "browser_download_url": "https://github.com/PortsMaster/PortMaster-GUI/releases/download/$1/decoy-notes.txt"
    },
    {
      "name": "${3:-PortMaster.zip}",
      "digest": "sha256:$2",
      "browser_download_url": "https://github.com/PortsMaster/PortMaster-GUI/releases/download/$1/${3:-PortMaster.zip}"
    }
  ]
}
EOF
}

# ---- sandbox SD card ----------------------------------------------------
SD="$TMP/sd"
seed_base_files() {
  mkdir -p "$SD/Emus/shared/PortMaster/files" "$SD/Emus/shared/PortMaster/patchedScripts"
  cp "$TMP/bin.zip" "$TMP/lib.zip" "$TMP/libs.zip" "$TMP/pylibs.zip" "$SD/Emus/shared/PortMaster/files/"
  echo 'certs'    > "$SD/Emus/shared/PortMaster/files/ca-certificates.crt"
  echo 'py-patch' > "$SD/Emus/shared/PortMaster/files/disable_python_function.py"
  echo 'patched'  > "$SD/Emus/shared/PortMaster/patchedScripts/SteelAssault.sh"
}
mkdir -p "$SD/.userdata/tg5050/logs"
seed_base_files

# ---- PATH shims ---------------------------------------------------------
BIN="$TMP/bin"
mkdir -p "$BIN"
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
  *PortMaster.zip) cp "$TMP/PortMaster.zip" "\$out" ;;
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
  write_release_json "${FIX_TAG-2026.01.01-0000}" "${FIX_DIGEST:-$(sha "$TMP/PortMaster.zip")}" "${FIX_ASSET:-PortMaster.zip}"
  PATH="$BIN:$PATH" \
  PLATFORM=tg5050 \
  SDCARD_PATH="$SD" \
  LOGS_PATH="$SD/.userdata/tg5050/logs" \
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
  CATALOG_DIR="$ENTRY" \
  XTRAS_STATE_DIR="$SD/.userdata/shared/xtras" \
  bash "$ENTRY/uninstall.sh"
}

PM="$SD/Emus/shared/PortMaster"
PORTS_PAK="$SD/Emus/PORTS.pak"
TOOLS_PAK="$SD/Tools/PortMaster.pak"
OLD_TOOLS_PAK="$SD/Tools/tg5050/PortMaster.pak"
VER="$SD/.userdata/shared/xtras/portmaster.version"
CACHE="$SD/.userdata/tg5050/emulist_cache.txt"

# ---- 0. preflights ------------------------------------------------------
if NX_EXTRAS_UNZIP="$SD/.system/shared/bin/7zzs.aarch64" \
   run_install > "$TMP/log0.txt" 2>&1; then
  fail "preflight: missing unzip tool did not abort"
else pass "preflight: missing unzip tool aborts"; fi
grep -q 'system unzip tool missing' "$TMP/log0.txt" \
  && pass "preflight: unzip message printed" || fail "preflight: unzip message missing"
! grep -q 'Downloading' "$TMP/log0.txt" \
  && pass "preflight: no download attempted" || fail "preflight: a download was attempted"

mv "$PM/files/bin.zip" "$TMP/bin.zip.away"
if run_install > "$TMP/log0b.txt" 2>&1; then
  fail "preflight: missing bundled deps did not abort"
else pass "preflight: missing bundled deps aborts"; fi
grep -q 'bundled runtime files missing' "$TMP/log0b.txt" \
  && pass "preflight: bundled-deps message printed" || fail "preflight: bundled-deps message missing"
mv "$TMP/bin.zip.away" "$PM/files/bin.zip"

rm -f "$ENTRY/pak/portmaster.elf"
if run_install > "$TMP/log0c.txt" 2>&1; then
  fail "preflight: missing pak payload did not abort"
else pass "preflight: missing pak payload aborts"; fi
grep -q 'catalog pak payload missing' "$TMP/log0c.txt" \
  && pass "preflight: pak-payload message printed" || fail "preflight: pak-payload message missing"
echo 'fake-elf' > "$ENTRY/pak/portmaster.elf"

# ---- 1. fresh install ---------------------------------------------------
echo 'stale' > "$CACHE"
if run_install > "$TMP/log1.txt" 2>&1; then pass "fresh install exits 0"
else fail "fresh install exited non-zero: $(tail -3 "$TMP/log1.txt")"; fi
[ "$(cat "$PM/pugwash" 2>/dev/null)" = "pugwash-gui" ] \
  && pass "pugwash extracted" || fail "pugwash missing"
[ -f "$PM/bin/busybox" ] && [ -f "$PM/lib/libfoo.so.1" ] && [ -f "$PM/libs/marker-libs.txt" ] \
  && pass "bin/lib/libs deps extracted" || fail "bin/lib/libs deps missing"
[ -f "$PM/pylibs/harbourmaster/marker.py" ] && [ -f "$PM/exlibs/marker.txt" ] \
  && pass "pylibs extracted to runtime root" || fail "pylibs missing"
[ "$(cat "$PM/ssl/certs/ca-certificates.crt" 2>/dev/null)" = "certs" ] \
  && pass "SSL certs installed" || fail "SSL certs missing"
[ -f "$PM/disable_python_function.py" ] \
  && pass "patch helper installed" || fail "patch helper missing"
# compat quarantine: newer glib must leave lib/ (it breaks pugwash's SDL2)
[ -f "$PM/lib/compat/libglib-2.0.so.0" ] && [ ! -e "$PM/lib/libglib-2.0.so.0" ] \
  && pass "compat lib quarantined to lib/compat/" || fail "compat lib not quarantined"
# unversioned copies: created in lib/ except GPU libs; all created in compat/
[ -f "$PM/lib/libfoo.so" ] \
  && pass "unversioned .so copy created in lib/" || fail "unversioned .so copy missing in lib/"
[ ! -e "$PM/lib/libEGL.so" ] \
  && pass "GPU lib skipped for unversioned copy" || fail "unversioned GPU lib copy created (would crash gl4es)"
[ -f "$PM/lib/compat/libglib-2.0.so" ] \
  && pass "unversioned .so copy created in lib/compat/" || fail "unversioned .so copy missing in lib/compat/"
# busybox wrappers: generated for free names, sh skipped, real binaries kept
[ -x "$PM/bin/ls" ] && grep -q 'busybox ls' "$PM/bin/ls" \
  && pass "busybox wrapper generated" || fail "busybox wrapper missing"
[ "$(cat "$PM/bin/gzip")" = "real-gzip-binary" ] \
  && pass "real binary not clobbered by wrapper" || fail "real binary clobbered"
[ ! -e "$PM/bin/sh" ] \
  && pass "sh wrapper skipped" || fail "sh wrapper was created"
[ -f "$PM/bin/busybox_wrappers.done" ] \
  && pass "wrapper marker written" || fail "wrapper marker missing"
grep -q '"disclaimer": true' "$PM/config/config.json" 2>/dev/null \
  && pass "default config written" || fail "default config missing"
cmp -s "$PORTS_PAK/launch.sh" "$ENTRY/pak/ports_launch.sh" \
  && pass "PORTS.pak launcher is the catalog ports_launch.sh" || fail "PORTS.pak launcher wrong"
[ -x "$TOOLS_PAK/launch.sh" ] && [ -x "$TOOLS_PAK/ports_launch.sh" ] && [ -x "$TOOLS_PAK/portmaster.elf" ] \
  && pass "Tools pak installed with exec bits" || fail "Tools pak incomplete"
[ -d "$SD/Roms/Ports (PORTS)" ] \
  && pass "Ports Roms folder created" || fail "Ports Roms folder missing"
[ ! -e "$CACHE" ] \
  && pass "emu list cache invalidated" || fail "emu list cache survived"
[ "$(cat "$VER" 2>/dev/null)" = "2026.01.01-0000" ] \
  && pass "resolved tag written to version record" || fail "version record wrong: '$(cat "$VER" 2>/dev/null)'"
grep -q '@90 ' "$TMP/log1.txt" \
  && pass "install emits @NN progress hints" || fail "no @NN progress hint found"
grep -q 'Latest release: 2026.01.01-0000' "$TMP/log1.txt" \
  && pass "resolved release announced in output" || fail "no resolved-release line"
[ ! -d "$SD/.extras_tmp" ] \
  && pass "temp dir cleaned up" || fail "temp dir left behind"

# ---- 2. reinstall keeps user config, absorbs the old platform-folder pak -
echo '{"user": true}' > "$PM/config/config.json"
mkdir -p "$OLD_TOOLS_PAK" "$SD/Tools/tg5050/Other.pak"
echo 'old-elf'   > "$OLD_TOOLS_PAK/portmaster.elf"
echo 'other-pak' > "$SD/Tools/tg5050/Other.pak/launch.sh"
if run_install > "$TMP/log2.txt" 2>&1; then pass "reinstall exits 0"
else fail "reinstall exited non-zero: $(tail -3 "$TMP/log2.txt")"; fi
[ "$(cat "$PM/config/config.json")" = '{"user": true}' ] \
  && pass "reinstall: user config kept" || fail "reinstall: user config clobbered"
[ ! -d "$OLD_TOOLS_PAK" ] \
  && pass "reinstall: old platform-folder pak absorbed" || fail "reinstall: old platform-folder pak still present"
[ -f "$SD/Tools/tg5050/Other.pak/launch.sh" ] \
  && pass "reinstall: unrelated platform-folder pak kept" || fail "reinstall: unrelated pak damaged"
rm -rf "$SD/Tools/tg5050"

# ---- 3a. checksum mismatch fails closed BEFORE touching the runtime -----
PUGWASH_BEFORE="$(cat "$PM/pugwash")"
if FIX_DIGEST="2222222222222222222222222222222222222222222222222222222222222222" \
   run_install > "$TMP/log3a.txt" 2>&1; then
  fail "bad digest did not abort"
else pass "bad digest aborts"; fi
grep -q 'checksum mismatch' "$TMP/log3a.txt" \
  && pass "bad digest: message printed" || fail "bad digest: message missing"
[ "$(cat "$PM/pugwash" 2>/dev/null)" = "$PUGWASH_BEFORE" ] \
  && pass "bad digest: existing install untouched" || fail "bad digest: existing install damaged"

# ---- 3b. mid-install failure strips the runtime back to BASE files ------
echo 'not-a-zip' > "$PM/files/bin.zip"
if run_install > "$TMP/log3b.txt" 2>&1; then
  fail "corrupt dep zip did not abort"
else pass "corrupt dep zip aborts"; fi
[ ! -e "$PM/pugwash" ] \
  && pass "failed install: half-written runtime removed" || fail "failed install: runtime left launchable"
[ -f "$PM/files/lib.zip" ] && [ -f "$PM/patchedScripts/SteelAssault.sh" ] \
  && pass "failed install: BASE files/ + patchedScripts/ kept" || fail "failed install: BASE payload lost"
[ ! -d "$TOOLS_PAK" ] && [ ! -d "$PORTS_PAK" ] \
  && pass "failed install: launchers pulled" || fail "failed install: launchers left behind"
cp "$TMP/bin.zip" "$PM/files/bin.zip"

# ---- 4. uninstall --------------------------------------------------------
run_install > /dev/null 2>&1 || fail "reseed install for uninstall test failed"
mkdir -p "$SD/Roms/Ports (PORTS)/.ports/somegame" "$SD/Emus/tg5050/PORTS.pak" "$SD/Emus/tg5050/N64.pak" "$OLD_TOOLS_PAK"
echo 'old-tool' > "$OLD_TOOLS_PAK/portmaster.elf"
echo 'port-script' > "$SD/Roms/Ports (PORTS)/SomePort.sh"
echo 'legacy'      > "$SD/Emus/tg5050/PORTS.pak/launch.sh"
echo 'n64'         > "$SD/Emus/tg5050/N64.pak/launch.sh"
echo 'stale'       > "$CACHE"
if run_uninstall > "$TMP/log4.txt" 2>&1; then pass "uninstall exits 0"
else fail "uninstall exited non-zero: $(tail -3 "$TMP/log4.txt")"; fi
[ ! -d "$TOOLS_PAK" ]  && pass "uninstall: Tools pak removed"      || fail "uninstall: Tools pak still present"
[ ! -d "$OLD_TOOLS_PAK" ] && [ ! -d "$SD/Tools/tg5050" ] \
  && pass "uninstall: old platform-folder pak + empty dir removed" || fail "uninstall: old platform-folder remnants left"
[ ! -e "$VER" ]        && pass "uninstall: version marker removed" || fail "uninstall: version marker still present"
[ ! -e "$PM/pugwash" ] && [ ! -d "$PM/bin" ] && [ ! -f "$PM/config/config.json" ] \
  && pass "uninstall: runtime removed" || fail "uninstall: runtime remnants left"
[ -f "$PM/files/bin.zip" ] && [ -f "$PM/patchedScripts/SteelAssault.sh" ] \
  && pass "uninstall: BASE files/ + patchedScripts/ kept" || fail "uninstall: BASE payload lost"
[ ! -d "$PORTS_PAK" ]  && pass "uninstall: PORTS.pak removed"      || fail "uninstall: PORTS.pak still present"
[ ! -d "$SD/Emus/tg5050/PORTS.pak" ] \
  && pass "uninstall: legacy platform PORTS.pak removed" || fail "uninstall: legacy PORTS.pak still present"
[ -f "$SD/Emus/tg5050/N64.pak/launch.sh" ] \
  && pass "uninstall: unrelated platform pak kept" || fail "uninstall: unrelated pak damaged"
[ -f "$SD/Roms/Ports (PORTS)/SomePort.sh" ] && [ -d "$SD/Roms/Ports (PORTS)/.ports/somegame" ] \
  && pass "uninstall: installed ports kept" || fail "uninstall: installed ports lost"
[ ! -e "$CACHE" ]      && pass "uninstall: emu list cache invalidated" || fail "uninstall: cache survived"
if run_uninstall > /dev/null 2>&1; then pass "uninstall is idempotent"
else fail "second uninstall exited non-zero"; fi

say ""
if [ "$FAILS" -eq 0 ]; then say "ALL PASS"; else say "$FAILS FAILURE(S)"; exit 1; fi
