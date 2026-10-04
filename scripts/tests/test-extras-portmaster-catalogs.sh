#!/usr/bin/env bash
# Host test for the third-party PortMaster catalog entries in Xtras
# (skeleton/SYSTEM/<plat>/paks/Tools/Xtras.pak/catalog/portmaster-{nextos,rhh}):
# each drops its *.source.json into PortMaster's config/, RHH also fetches
# gmtoolkit (digest-verified), and uninstall takes both back out. Runs the
# real install.sh/uninstall.sh against a fake card with a wget shim serving
# fixtures.
set -u
cd "$(dirname "$0")/../.."
ROOT="$PWD"
FAILS=0
say()  { printf '%s\n' "$*"; }
pass() { say "PASS: $*"; }
fail() { say "FAIL: $*"; FAILS=$((FAILS+1)); }

CAT40="$ROOT/skeleton/SYSTEM/tg5040/paks/Tools/Xtras.pak/catalog"
CAT50="$ROOT/skeleton/SYSTEM/tg5050/paks/Tools/Xtras.pak/catalog"
for id in portmaster-nextos portmaster-rhh; do
  diff -r "$CAT40/$id" "$CAT50/$id" >/dev/null \
    && pass "$id: tg5040 and tg5050 entries identical" || fail "$id: tg5040/tg5050 entries differ"
  grep -q '^version_source=internal$' "$CAT50/$id/meta.txt" && grep -q '^version=' "$CAT50/$id/meta.txt" \
    && pass "$id: internal version (bump to push an update)" || fail "$id: meta.txt lacks an internal version"
  [ -x "$CAT50/$id/install.sh" ] && [ -x "$CAT50/$id/uninstall.sh" ] \
    && pass "$id: scripts executable" || fail "$id: scripts not executable"
done
python3 -c "import json,sys; [json.load(open(f)) for f in sys.argv[1:]]" \
  "$CAT50/portmaster-nextos/040_nextos.source.json" "$CAT50/portmaster-rhh/030_rhh.source.json" \
  && pass "source files are valid JSON" || fail "source file JSON invalid"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SD="$TMP/sd"
PM="$SD/Emus/shared/PortMaster"
STATE="$SD/.userdata/shared/xtras"
BIN="$TMP/bin"
mkdir -p "$BIN" "$SD/.userdata/tg5050"

# gmtoolkit release fixture: a zip like upstream's and the API JSON for it.
mkdir -p "$TMP/gmt"
printf 'gmtoolkit-binary\n' > "$TMP/gmt/gmtoolkit.aarch64"
printf 'license\n' > "$TMP/gmt/gmtoolkit.LICENSE.txt"
(cd "$TMP/gmt" && zip -q "$TMP/gmtoolkit-aarch64.zip" gmtoolkit.aarch64 gmtoolkit.LICENSE.txt)
sha() { shasum -a 256 "$1" | cut -d' ' -f1; }
write_release_json() { # $1 = digest
  cat > "$TMP/release.json" <<EOF
{
  "tag_name": "latest",
  "assets": [
    {
      "name": "gmtoolkit-linux-x86_64.zip",
      "digest": "sha256:0000",
      "browser_download_url": "https://example.test/gmtoolkit-linux-x86_64.zip"
    },
    {
      "name": "gmtoolkit-aarch64.zip",
      "digest": "sha256:$1",
      "browser_download_url": "https://example.test/gmtoolkit-aarch64.zip"
    }
  ]
}
EOF
}
cat > "$BIN/wget" <<SHIM
#!/usr/bin/env bash
echo "\$*" >> "$TMP/wget.args"
out=""; url=""
while [ \$# -gt 0 ]; do
  case "\$1" in
    -O) out="\$2"; shift ;;
    -*) ;;
    *) url="\$1" ;;
  esac
  shift
done
echo "\$url" >> "$TMP/wget.log"
case "\$url" in
  *JeodC/gmtoolkit/releases/latest) cp "$TMP/release.json" "\$out" ;;
  *gmtoolkit-aarch64.zip) cp "$TMP/gmtoolkit-aarch64.zip" "\$out" ;;
  *) echo "wget shim: unknown url \$url" >&2; exit 1 ;;
esac
SHIM
chmod +x "$BIN/wget"
cat > "$BIN/sha256sum" <<'SHIM'
#!/usr/bin/env bash
shasum -a 256 "$@"
SHIM
chmod +x "$BIN/sha256sum"

run() { # $1 = entry id, $2 = install|uninstall
  PATH="$BIN:$PATH" \
  PLATFORM=tg5050 \
  SDCARD_PATH="$SD" \
  LOGS_PATH="$SD/.userdata/tg5050/logs" \
  CATALOG_DIR="$CAT50/$1" \
  XTRAS_STATE_DIR="$STATE" \
  NX_EXTRAS_UNZIP=unzip \
  bash "$CAT50/$1/$2.sh"
}

# ---- 1. PortMaster not installed: both refuse, nothing written ------------
for id in portmaster-nextos portmaster-rhh; do
  : > "$TMP/wget.log"
  if run "$id" install > "$TMP/log.txt" 2>&1; then fail "$id: installed without PortMaster"
  else pass "$id: refuses without PortMaster"; fi
  grep -q 'install PortMaster first' "$TMP/log.txt" && pass "$id: says to install PortMaster" || fail "$id: no PortMaster message"
  [ ! -e "$PM/config" ] && pass "$id: nothing written" || fail "$id: wrote into PortMaster"
  [ ! -s "$TMP/wget.log" ] && pass "$id: no network before the preflight" || fail "$id: went online first"
done

mkdir -p "$PM/config"
echo 'pugwash' > "$PM/pugwash"

# ---- 2. NextOS ----------------------------------------------------------------
if run portmaster-nextos install > "$TMP/log.txt" 2>&1; then pass "nextos: install exits 0"
else fail "nextos: install failed: $(tail -2 "$TMP/log.txt")"; fi
cmp -s "$PM/config/040_nextos.source.json" "$CAT50/portmaster-nextos/040_nextos.source.json" \
  && pass "nextos: source file in PortMaster's config/" || fail "nextos: source file missing"
grep -q '^@100 ' "$TMP/log.txt" && pass "nextos: @NN progress hints" || fail "nextos: no progress hints"
[ ! -e "$STATE/portmaster-nextos.version" ] && pass "nextos: version record left to extras.elf" || fail "nextos: install.sh wrote the version record"
# harbourmaster rewrites the file with its cache; a reinstall (version bump) resets it
echo '{"prefix": "nextos", "data": {"cached": 1}}' > "$PM/config/040_nextos.source.json"
run portmaster-nextos install > /dev/null 2>&1
cmp -s "$PM/config/040_nextos.source.json" "$CAT50/portmaster-nextos/040_nextos.source.json" \
  && pass "nextos: reinstall puts back the shipped source file" || fail "nextos: reinstall kept the old file"
mkdir -p "$PM/config/images_nextos" "$PM/config/images_pm" "$SD/Roms/Ports (PORTS)/.ports/oceanhorn"
echo shot > "$PM/config/images_nextos/oceanhorn.screenshot.jpg"
mkdir -p "$STATE"; echo x > "$STATE/portmaster-nextos.version"
if run portmaster-nextos uninstall > "$TMP/log.txt" 2>&1; then pass "nextos: uninstall exits 0"
else fail "nextos: uninstall failed: $(tail -2 "$TMP/log.txt")"; fi
[ ! -e "$PM/config/040_nextos.source.json" ] && [ ! -e "$PM/config/images_nextos" ] \
  && pass "nextos: source file and cached images removed" || fail "nextos: catalog left in PortMaster"
[ -d "$PM/config/images_pm" ] && [ -f "$PM/pugwash" ] && pass "nextos: PortMaster's own catalog untouched" || fail "nextos: uninstall touched PortMaster"
[ -d "$SD/Roms/Ports (PORTS)/.ports/oceanhorn" ] && pass "nextos: installed ports kept" || fail "nextos: installed port removed"
[ ! -e "$STATE/portmaster-nextos.version" ] && pass "nextos: version marker removed" || fail "nextos: version marker kept"

# ---- 3. RHH -------------------------------------------------------------------
# TLS is verified against PortMaster's CA bundle: no bundle, no download.
write_release_json "$(sha "$TMP/gmtoolkit-aarch64.zip")"
: > "$TMP/wget.log"
if run portmaster-rhh install > "$TMP/log.txt" 2>&1; then fail "rhh: installed without a CA bundle"
else pass "rhh: no CA bundle aborts"; fi
grep -q 'certificate bundle is missing' "$TMP/log.txt" && pass "rhh: CA bundle message" || fail "rhh: no CA bundle message"
[ ! -s "$TMP/wget.log" ] && pass "rhh: no network without a CA bundle" || fail "rhh: went online without a CA bundle"
mkdir -p "$PM/ssl/certs"; echo 'ca' > "$PM/ssl/certs/ca-certificates.crt"

write_release_json "deadbeef"
if run portmaster-rhh install > "$TMP/log.txt" 2>&1; then fail "rhh: installed with a bad digest"
else pass "rhh: bad digest aborts"; fi
grep -q 'checksum mismatch' "$TMP/log.txt" && pass "rhh: checksum message" || fail "rhh: no checksum message"
[ ! -e "$PM/gmtoolkit.aarch64" ] && [ ! -e "$PM/config/030_rhh.source.json" ] \
  && pass "rhh: failed install leaves nothing" || fail "rhh: failed install left files"
[ ! -e "$SD/.extras_tmp" ] && pass "rhh: temp dir cleaned on failure" || fail "rhh: temp dir left"

write_release_json "$(sha "$TMP/gmtoolkit-aarch64.zip")"
: > "$TMP/wget.log"; : > "$TMP/wget.args"
if run portmaster-rhh install > "$TMP/log.txt" 2>&1; then pass "rhh: install exits 0"
else fail "rhh: install failed: $(tail -2 "$TMP/log.txt")"; fi
[ "$(cat "$PM/gmtoolkit.aarch64" 2>/dev/null)" = 'gmtoolkit-binary' ] && [ -x "$PM/gmtoolkit.aarch64" ] \
  && pass "rhh: gmtoolkit.aarch64 in \$controlfolder, executable" || fail "rhh: gmtoolkit missing"
[ -f "$PM/gmtoolkit.LICENSE.txt" ] && pass "rhh: gmtoolkit licence kept" || fail "rhh: licence missing"
grep -q 'gmtoolkit-aarch64.zip$' "$TMP/wget.log" && ! grep -q 'x86_64' "$TMP/wget.log" \
  && pass "rhh: the aarch64 asset picked" || fail "rhh: wrong asset: $(tr '\n' ' ' < "$TMP/wget.log")"
[ "$(grep -c -- "--check-certificate=on --ca-certificate=$PM/ssl/certs/ca-certificates.crt" "$TMP/wget.args")" = 2 ] \
  && ! grep -q -- '--no-check-certificate' "$TMP/wget.args" \
  && pass "rhh: both requests verify TLS against PortMaster's CA bundle" || fail "rhh: TLS not verified: $(tr '\n' ' ' < "$TMP/wget.args")"
cmp -s "$PM/config/030_rhh.source.json" "$CAT50/portmaster-rhh/030_rhh.source.json" \
  && pass "rhh: source file in PortMaster's config/" || fail "rhh: source file missing"
[ ! -e "$SD/.extras_tmp" ] && [ ! -e "$PM/gmtoolkit.aarch64.nxtmp" ] && pass "rhh: no temp files left" || fail "rhh: temp files left"
mkdir -p "$PM/config/images_rhh"; echo x > "$STATE/portmaster-rhh.version"
if run portmaster-rhh uninstall > "$TMP/log.txt" 2>&1; then pass "rhh: uninstall exits 0"
else fail "rhh: uninstall failed: $(tail -2 "$TMP/log.txt")"; fi
[ ! -e "$PM/config/030_rhh.source.json" ] && [ ! -e "$PM/config/images_rhh" ] && [ ! -e "$PM/gmtoolkit.aarch64" ] \
  && [ ! -e "$PM/gmtoolkit.LICENSE.txt" ] && [ ! -e "$STATE/portmaster-rhh.version" ] \
  && pass "rhh: catalog, gmtoolkit and marker removed" || fail "rhh: uninstall left files"

# ---- 4. PortMaster's uninstall drops both entries' markers ----------------
grep -q 'portmaster-nextos.version' "$CAT50/portmaster/uninstall.sh" && grep -q 'portmaster-rhh.version' "$CAT50/portmaster/uninstall.sh" \
  && grep -q 'portmaster-nextos.version' "$CAT40/portmaster/uninstall.sh" \
  && pass "portmaster uninstall clears the catalog entries' markers" || fail "portmaster uninstall leaves catalog markers"

[ "$FAILS" = 0 ] && say "ALL PASS" || say "$FAILS FAILURE(S)"
exit "$FAILS"
